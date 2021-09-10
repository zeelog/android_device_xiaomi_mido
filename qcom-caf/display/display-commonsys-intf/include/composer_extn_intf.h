/*
 * Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
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

#ifndef __COMPOSER_EXTN_INTF_H__
#define __COMPOSER_EXTN_INTF_H__

#include <dlfcn.h>
#include "frame_scheduler_intf.h"
#include "display_extn_intf.h"
#include "phase_offset_extn_intf.h"

#define COMPOSER_EXTN_REV_MAJOR (1)
#define COMPOSER_EXTN_REV_MINOR (0)
#define COMPOSER_EXTN_VERSION_TAG ((uint16_t) ((COMPOSER_EXTN_REV_MAJOR << 8) \
                                              | COMPOSER_EXTN_REV_MINOR))

namespace composer {

class ComposerExtnIntf {
 public:
  virtual int CreateFrameScheduler(FrameSchedulerIntf **intf) = 0;
  virtual void DestroyFrameScheduler(FrameSchedulerIntf *intf) = 0;
  virtual int CreateDisplayExtn(DisplayExtnIntf **intf) = 0;
  virtual void DestroyDisplayExtn(DisplayExtnIntf *intf) = 0;
  virtual int CreatePhaseOffsetExtn(PhaseOffsetExtnIntf **intf) = 0;
  virtual void DestroyPhaseOffsetExtn(PhaseOffsetExtnIntf *intf) = 0;
 protected:
  virtual ~ComposerExtnIntf() { }
};

class ComposerExtnLib {
 public:
  static ComposerExtnIntf * GetInstance() {
    return g_composer_ext_lib_.composer_ext_intf_;
  }

 private:
  const char *lib_name = "libcomposerextn.qti.so";

  typedef int (*CreateComposerExtn)(uint16_t version, ComposerExtnIntf **intf);
  typedef void (*DestroyComposerExtn)(ComposerExtnIntf *intf);

  ComposerExtnLib() {
    lib_obj_ = ::dlopen(lib_name, RTLD_NOW);
    if (!lib_obj_) {
      return;
    }

    create_composer_ext_fn_ = reinterpret_cast<CreateComposerExtn>(
                                    ::dlsym(lib_obj_, "CreateComposerExtn"));
    destroy_composer_ext_fn_ = reinterpret_cast<DestroyComposerExtn>(
                                    ::dlsym(lib_obj_, "DestroyComposerExtn"));
    if (create_composer_ext_fn_ && destroy_composer_ext_fn_) {
      create_composer_ext_fn_(COMPOSER_EXTN_VERSION_TAG, &composer_ext_intf_);
    }
  }

  ~ComposerExtnLib() {
    if (composer_ext_intf_) {
      destroy_composer_ext_fn_(composer_ext_intf_);
    }

    if (lib_obj_) {
      ::dlclose(lib_obj_);
    }
  }

  static ComposerExtnLib g_composer_ext_lib_;
  void *lib_obj_ = nullptr;
  CreateComposerExtn create_composer_ext_fn_ = nullptr;
  DestroyComposerExtn destroy_composer_ext_fn_ = nullptr;
  ComposerExtnIntf *composer_ext_intf_ = nullptr;
};

}  // namespace composer

#endif  // __COMPOSER_EXTN_INTF_H__
