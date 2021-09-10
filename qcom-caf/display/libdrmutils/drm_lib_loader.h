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

#ifndef __DRM_LIB_LOADER_H__
#define __DRM_LIB_LOADER_H__

#include <drm_interface.h>
#include <mutex>

namespace drm_utils {

class DRMLibLoader {
 public:
  ~DRMLibLoader();
  bool IsLoaded() { return is_loaded_; }
  sde_drm::GetDRMManager FuncGetDRMManager() { return func_get_drm_manager_; }
  sde_drm::DestroyDRMManager FuncDestroyDRMManager() { return func_destroy_drm_manager_; }

  static DRMLibLoader *GetInstance();
  static void Destroy();

 private:
  DRMLibLoader();
  bool Open(const char *lib_name);
  bool Sym(const char *func_name, void **func_ptr);

  void *lib_ = {};
  sde_drm::GetDRMManager func_get_drm_manager_ = {};
  sde_drm::DestroyDRMManager func_destroy_drm_manager_ = {};
  bool is_loaded_ = false;

  static DRMLibLoader *s_instance;  // Singleton instance
  static std::mutex s_lock;
};

}  // namespace drm_utils

#endif  // __DRM_LIB_LOADER_H__
