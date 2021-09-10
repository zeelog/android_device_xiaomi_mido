/*
* Copyright (c) 2015 - 2017, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without modification, are permitted
* provided that the following conditions are met:
*    * Redistributions of source code must retain the above copyright notice, this list of
*      conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above copyright notice, this list of
*      conditions and the following disclaimer in the documentation and/or other materials provided
*      with the distribution.
*    * Neither the name of The Linux Foundation nor the names of its contributors may be used to
*      endorse or promote products derived from this software without specific prior written
*      permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
* STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef __HW_INFO_H__
#define __HW_INFO_H__

#include <core/sdm_types.h>
#include <core/core_interface.h>
#include <private/hw_info_types.h>
#include <linux/msm_mdp.h>
#include <bitset>

#include "hw_info_interface.h"

#ifndef MDP_IMGTYPE_END
#define MDP_IMGTYPE_LIMIT1 0x100
#endif

namespace sdm {

class HWInfo: public HWInfoInterface {
 public:
  virtual ~HWInfo() { delete hw_resource_; }
  virtual DisplayError GetHWResourceInfo(HWResourceInfo *hw_resource);
  virtual DisplayError GetFirstDisplayInterfaceType(HWDisplayInterfaceInfo *hw_disp_info);

 private:
  virtual DisplayError GetHWRotatorInfo(HWResourceInfo *hw_resource);
  virtual DisplayError GetMDSSRotatorInfo(HWResourceInfo *hw_resource);
  virtual DisplayError GetV4L2RotatorInfo(HWResourceInfo *hw_resource);

  // TODO(user): Read Mdss version from the driver
  static const int kHWMdssVersion5 = 500;  // MDSS_V5
  static const int kMaxStringLength = 1024;
  // MDP Capabilities are replicated across all frame buffer devices.
  // However, we rely on reading the capabalities from fbO since this
  // is guaranteed to be available.
  static const int kHWCapabilitiesNode = 0;
  static const std::bitset<8> kDefaultFormatSupport[kHWSubBlockMax][
                                                        BITS_TO_BYTES(MDP_IMGTYPE_LIMIT1)];
  static constexpr const char *kRotatorCapsPath = "/sys/devices/virtual/rotator/mdss_rotator/caps";
  static constexpr const char *kBWModeBitmap
                                  = "/sys/devices/virtual/graphics/fb0/mdp/bw_mode_bitmap";

  static int ParseString(const char *input, char *tokens[], const uint32_t max_token,
                         const char *delim, uint32_t *count);
  DisplayError GetDynamicBWLimits(HWResourceInfo *hw_resource);
  LayerBufferFormat GetSDMFormat(int mdp_format);
  void InitSupportedFormatMap(HWResourceInfo *hw_resource);
  void ParseFormats(char *tokens[], uint32_t token_count, HWSubBlockType sub_block_type,
                    HWResourceInfo *hw_resource);
  void PopulateSupportedFormatMap(const std::bitset<8> *format_supported, uint32_t format_count,
                                  HWSubBlockType sub_blk_type, HWResourceInfo *hw_resource);
  HWResourceInfo *hw_resource_ = NULL;
};

}  // namespace sdm

#endif  // __HW_INFO_H__

