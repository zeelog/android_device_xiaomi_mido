/*
* Copyright (c) 2016-2017, The Linux Foundation. All rights reserved.
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

#ifndef __HW_SCALE_DRM_H__
#define __HW_SCALE_DRM_H__

#include <stdint.h>
#include <stdlib.h>
#include <drm.h>
// The 3 headers above are a workaround to prevent kernel drm.h from being used that has the
// "virtual" keyword used for a variable. In future replace libdrm version drm.h with kernel
// version drm/drm.h
#include <drm/sde_drm.h>
#include <private/hw_info_types.h>

namespace sdm {

struct SDEScaler {
  struct sde_drm_scaler_v2 scaler_v2 = {};
  // More here, maybe in a union
};

class HWScaleDRM {
 public:
  enum class Version { V2 };
  explicit HWScaleDRM(Version v) : version_(v) {}
  void SetPlaneScaler(const HWScaleData &scale, SDEScaler *scaler);

 private:
  void SetPlaneScalerV2(const HWScaleData &scale, sde_drm_scaler_v2 *scaler_v2);

  Version version_ = Version::V2;
};

}  // namespace sdm

#endif  // __HW_SCALE_DRM_H__
