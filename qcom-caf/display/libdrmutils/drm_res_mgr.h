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

#ifndef __DRM_RES_MGR_H__
#define __DRM_RES_MGR_H__

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <mutex>

namespace drm_utils {

class DRMResMgr {
 public:
  /* Returns the default connector id for primary panel */
  void GetConnectorId(uint32_t *id) { *id = conn_->connector_id; }
  /* Returns the default crtc id for primary pipeline */
  void GetCrtcId(uint32_t *id) { *id = crtc_->crtc_id; }
  /* Returns the default mode currently used by the connector */
  void GetMode(drmModeModeInfo *mode) { *mode = conn_->modes[0]; }
  /* Returns the panel dimensions in mm */
  void GetDisplayDimInMM(uint32_t *w, uint32_t *h) {
    *w = conn_->mmWidth;
    *h = conn_->mmHeight;
  }

  /* Creates and initializes an instance of DRMResMgr. On success, returns a pointer to it, on
   * failure returns -ENODEV */
  static int GetInstance(DRMResMgr **res_mgr);

 private:
  int Init();

  drmModeRes *res_ = nullptr;
  drmModeConnector *conn_ = nullptr;
  drmModeEncoder *enc_ = nullptr;
  drmModeCrtc *crtc_ = nullptr;

  static DRMResMgr *s_instance;
  static std::mutex s_lock;
};

}  // namespace drm_utils

#endif  // __DRM_RES_MGR_H__
