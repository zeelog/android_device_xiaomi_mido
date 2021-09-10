/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *      contributors may be used to endorse or promote products derived
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

#ifndef __LAYER_EXTN_INTF_H__
#define __LAYER_EXTN_INTF_H__

#include <sys/types.h>

#include <vector>
#include <string>

namespace composer {

#define LAYER_EXTN_LIBRARY_NAME "liblayerext.qti.so"
#define CREATE_LAYER_EXTN_INTERFACE "CreateLayerExtnInterface"
#define DESTROY_LAYER_EXTN_INTERFACE "DestroyLayerExtnInterface"

#define LAYER_EXTN_REVISION_MAJOR (1)
#define LAYER_EXTN_REVISION_MINOR (0)
#define LAYER_EXTN_VERSION_TAG ((uint16_t) ((LAYER_EXTN_REVISION_MAJOR << 8) \
                                          | LAYER_EXTN_REVISION_MINOR))

class LayerExtnIntf {
 public:
  virtual ~LayerExtnIntf() = default;
  virtual int GetLayerClass(const std::string &name) = 0;
  virtual void UpdateLayerState(const std::vector<std::string> &layers, int num_layers) = 0;
};

typedef bool (*CreateLayerExtnInterface)(uint16_t version, LayerExtnIntf **interface);
typedef void (*DestroyLayerExtnInterface)(LayerExtnIntf *interface);

}  // namespace composer

#endif  // __LAYER_EXTN_INTF_H__
