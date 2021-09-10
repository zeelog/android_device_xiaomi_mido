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

#include <errno.h>

#include "drm_master.h"
#include "drm_res_mgr.h"

#define DEBUG 0
#define __CLASS__ "DRMResMgr"

using std::mutex;
using std::lock_guard;

namespace drm_utils {

DRMResMgr *DRMResMgr::s_instance = nullptr;
mutex DRMResMgr::s_lock;

static bool GetConnector(int dev_fd, drmModeRes *res, drmModeConnector **connector) {
  for (auto i = 0; i < res->count_connectors; i++) {
    drmModeConnector *conn = drmModeGetConnector(dev_fd, res->connectors[i]);
    if (conn && conn->connector_type == DRM_MODE_CONNECTOR_DSI && conn->count_modes &&
        conn->connection == DRM_MODE_CONNECTED) {
      *connector = conn;
      DRM_LOGI("Found connector %d", conn->connector_id);
      return true;
    }
  }

  return false;
}

static bool GetEncoder(int dev_fd, drmModeConnector *conn, drmModeEncoder **encoder) {
  for (auto i = 0; i < conn->count_encoders; i++) {
    drmModeEncoder *enc = drmModeGetEncoder(dev_fd, conn->encoders[i]);
    if (enc && enc->encoder_type == DRM_MODE_ENCODER_DSI) {
      *encoder = enc;
      DRM_LOGI("Found encoder %d", enc->encoder_id);
      return true;
    }
  }
  return false;
}

static bool GetCrtc(int dev_fd, drmModeRes *res, drmModeEncoder *enc, drmModeCrtc **crtc) {
  for (auto i = 0; i < res->count_crtcs; i++) {
    if (enc->possible_crtcs & (1 << i)) {
      drmModeCrtc *c = drmModeGetCrtc(dev_fd, res->crtcs[i]);
      if (c) {
        *crtc = c;
        DRM_LOGI("Found crtc %d", c->crtc_id);
        return true;
      }
    }
  }

  return false;
}

#define __CLASS__ "DRMResMgr"

int DRMResMgr::GetInstance(DRMResMgr **res_mgr) {
  lock_guard<mutex> obj(s_lock);

  if (!s_instance) {
    s_instance = new DRMResMgr();
    if (s_instance->Init() < 0) {
      delete s_instance;
      s_instance = nullptr;
      return -ENODEV;
    }
  }

  *res_mgr = s_instance;
  return 0;
}

int DRMResMgr::Init() {
  DRMMaster *master = nullptr;
  int dev_fd = -1;

  int ret = DRMMaster::GetInstance(&master);
  if (ret < 0) {
    return ret;
  }

  master->GetHandle(&dev_fd);
  drmModeRes *res = drmModeGetResources(dev_fd);
  if (res == nullptr) {
    DRM_LOGE("drmModeGetResources failed");
    return -ENODEV;
  }

  drmModeConnector *conn = nullptr;
  if (!GetConnector(dev_fd, res, &conn)) {
    DRM_LOGE("Failed to find a connector");
    return -ENODEV;
  }

  drmModeEncoder *enc = nullptr;
  if (!GetEncoder(dev_fd, conn, &enc)) {
    DRM_LOGE("Failed to find an encoder");
    drmModeFreeConnector(conn);
    return -ENODEV;
  }

  drmModeCrtc *crtc = nullptr;
  if (!GetCrtc(dev_fd, res, enc, &crtc)) {
    DRM_LOGE("Failed to find a crtc");
    drmModeFreeEncoder(enc);
    drmModeFreeConnector(conn);
    drmModeFreeResources(res);
    return -ENODEV;
  }

  res_ = res;
  conn_ = conn;
  enc_ = enc;
  crtc_ = crtc;

  return 0;
}

}  // namespace drm_utils
