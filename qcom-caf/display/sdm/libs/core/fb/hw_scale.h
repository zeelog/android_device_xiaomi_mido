/*
* Copyright (c) 2016, The Linux Foundation. All rights reserved.
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

#ifndef __HW_SCALE_H__
#define __HW_SCALE_H__

#include <linux/msm_mdp_ext.h>
#include <private/hw_info_types.h>

#include <cstring>
#include <array>
#include <map>

namespace sdm {

class HWScale {
 public:
  static DisplayError Create(HWScale **intf, bool has_qseed3);
  static DisplayError Destroy(HWScale *intf);

  virtual void SetHWScaleData(const HWScaleData &scale, uint32_t index,
                              mdp_layer_commit_v1 *mdp_commit, HWSubBlockType sub_block_type) = 0;
  virtual void* GetScaleDataRef(uint32_t index, HWSubBlockType sub_block_type) = 0;
  virtual void DumpScaleData(void *mdp_scale) = 0;
  virtual void ResetScaleParams() = 0;
 protected:
  virtual ~HWScale() { }
};

class HWScaleV1 : public HWScale {
 public:
  virtual void SetHWScaleData(const HWScaleData &scale, uint32_t index,
                              mdp_layer_commit_v1 *mdp_commit, HWSubBlockType sub_block_type);
  virtual void* GetScaleDataRef(uint32_t index, HWSubBlockType sub_block_type);
  virtual void DumpScaleData(void *mdp_scale);
  virtual void ResetScaleParams() { scale_data_v1_ = {}; }

 protected:
  ~HWScaleV1() {}
  std::array<mdp_scale_data, (kMaxSDELayers * 2)> scale_data_v1_ = {};
};

class HWScaleV2 : public HWScale {
 public:
  virtual void SetHWScaleData(const HWScaleData &scale, uint32_t index,
                              mdp_layer_commit_v1 *mdp_commit, HWSubBlockType sub_block_type);
  virtual void* GetScaleDataRef(uint32_t index, HWSubBlockType sub_block_type);
  virtual void DumpScaleData(void *mdp_scale);
  virtual void ResetScaleParams() { scale_data_v2_ = {}; dest_scale_data_v2_ = {}; }

 protected:
  ~HWScaleV2() {}
  std::array<mdp_scale_data_v2, (kMaxSDELayers * 2)> scale_data_v2_ = {};
  std::map<uint32_t, mdp_scale_data_v2> dest_scale_data_v2_ = {};

 private:
  uint32_t GetMDPAlphaInterpolation(HWAlphaInterpolation alpha_filter_cfg);
  uint32_t GetMDPScalingFilter(ScalingFilterConfig filter_cfg);
};

}  // namespace sdm

#endif  // __HW_SCALE_H__

