/* Copyright (c) 2015-2017, The Linux Foundataion. All rights reserved.
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
*
*/

#ifndef __COLOR_MANAGER_H__
#define __COLOR_MANAGER_H__

#include <stdlib.h>
#include <core/sdm_types.h>
#include <utils/locker.h>
#include <private/color_interface.h>
#include <utils/sys.h>
#include <utils/debug.h>
#include "hw_interface.h"

namespace sdm {

/*
 * ColorManager proxy to maintain necessary information to interact with underlying color service.
 * Each display object has its own proxy.
 */
class ColorManagerProxy {
 public:
  static DisplayError Init(const HWResourceInfo &hw_res_info);
  static void Deinit();

  /* Create ColorManagerProxy for this display object, following things need to be happening
   * 1. Instantiates concrete ColorInerface implementation.
   * 2. Pass all display object specific informations into it.
   * 3. Populate necessary resources.
   * 4. Need get panel name for hw_panel_info_.
   */
  static ColorManagerProxy *CreateColorManagerProxy(DisplayType type, HWInterface *hw_intf,
                                                    const HWDisplayAttributes &attribute,
                                                    const HWPanelInfo &panel_info);

  /* need reverse the effect of CreateColorManagerProxy. */
  ~ColorManagerProxy();

  DisplayError ColorSVCRequestRoute(const PPDisplayAPIPayload &in_payload,
                                    PPDisplayAPIPayload *out_payload,
                                    PPPendingParams *pending_action);
  DisplayError ApplyDefaultDisplayMode();
  DisplayError ColorMgrGetNumOfModes(uint32_t *mode_cnt);
  DisplayError ColorMgrGetModes(uint32_t *mode_cnt, SDEDisplayMode *modes);
  DisplayError ColorMgrSetMode(int32_t color_mode_id);
  DisplayError ColorMgrGetModeInfo(int32_t mode_id, AttrVal *query);
  DisplayError ColorMgrSetColorTransform(uint32_t length, const double *trans_data);
  DisplayError ColorMgrGetDefaultModeID(int32_t *mode_id);
  bool NeedsPartialUpdateDisable();
  DisplayError Commit();

 protected:
  ColorManagerProxy() {}
  ColorManagerProxy(DisplayType type, HWInterface *intf, const HWDisplayAttributes &attr,
                    const HWPanelInfo &info);

 private:
  static DynLib color_lib_;
  static CreateColorInterface create_intf_;
  static DestroyColorInterface destroy_intf_;
  static HWResourceInfo hw_res_info_;

  DisplayType device_type_;
  PPHWAttributes pp_hw_attributes_;
  HWInterface *hw_intf_;
  ColorInterface *color_intf_;
  PPFeaturesConfig pp_features_;
};

}  // namespace sdm

#endif  // __COLOR_MANAGER_H__
