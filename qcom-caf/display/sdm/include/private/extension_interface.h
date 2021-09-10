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

#ifndef __EXTENSION_INTERFACE_H__
#define __EXTENSION_INTERFACE_H__

#include <core/sdm_types.h>
#include <core/display_interface.h>

#include "partial_update_interface.h"
#include "strategy_interface.h"
#include "resource_interface.h"
#include "dpps_control_interface.h"

namespace sdm {

#define EXTENSION_LIBRARY_NAME "libsdmextension.so"
#define CREATE_EXTENSION_INTERFACE_NAME "CreateExtensionInterface"
#define DESTROY_EXTENSION_INTERFACE_NAME "DestroyExtensionInterface"

#define EXTENSION_REVISION_MAJOR (1)
#define EXTENSION_REVISION_MINOR (0)

#define EXTENSION_VERSION_TAG ((uint16_t) ((EXTENSION_REVISION_MAJOR << 8) \
                                          | EXTENSION_REVISION_MINOR))

class ExtensionInterface;

typedef DisplayError (*CreateExtensionInterface)(uint16_t version, ExtensionInterface **interface);
typedef DisplayError (*DestroyExtensionInterface)(ExtensionInterface *interface);

class ExtensionInterface {
 public:
  virtual DisplayError CreatePartialUpdate(DisplayType type, const HWResourceInfo &hw_resource_info,
                                           const HWPanelInfo &hw_panel_info,
                                           const HWMixerAttributes &mixer_attributes,
                                           const HWDisplayAttributes &display_attributes,
                                           const DisplayConfigVariableInfo &fb_config,
                                           PartialUpdateInterface **interface) = 0;
  virtual DisplayError DestroyPartialUpdate(PartialUpdateInterface *interface) = 0;

  virtual DisplayError CreateStrategyExtn(DisplayType type, BufferAllocator *buffer_allocator,
                                          const HWResourceInfo &hw_resource_info,
                                          const HWPanelInfo &hw_panel_info,
                                          const HWMixerAttributes &mixer_attributes,
                                          const DisplayConfigVariableInfo &fb_config,
                                          StrategyInterface **interface) = 0;
  virtual DisplayError DestroyStrategyExtn(StrategyInterface *interface) = 0;

  virtual DisplayError CreateResourceExtn(const HWResourceInfo &hw_resource_info,
                                          BufferAllocator *buffer_allocator,
                                          BufferSyncHandler *buffer_sync_handler,
                                          ResourceInterface **interface) = 0;
  virtual DisplayError DestroyResourceExtn(ResourceInterface *interface) = 0;
  virtual DisplayError CreateDppsControlExtn(DppsControlInterface **dpps_control_interface,
                                             SocketHandler *socket_handler) = 0;
  virtual DisplayError DestroyDppsControlExtn(DppsControlInterface *interface) = 0;

 protected:
  virtual ~ExtensionInterface() { }
};

}  // namespace sdm

#endif  // __EXTENSION_INTERFACE_H__

