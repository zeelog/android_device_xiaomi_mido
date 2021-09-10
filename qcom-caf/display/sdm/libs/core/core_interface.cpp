/*
* Copyright (c) 2014 - 2015, The Linux Foundation. All rights reserved.
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

#include <utils/locker.h>
#include <utils/constants.h>
#include <utils/debug.h>
#include <core/buffer_sync_handler.h>

#include "core_impl.h"

#define __CLASS__ "CoreInterface"

#define GET_REVISION(version) (version >> 16)
#define GET_DATA_ALIGNMENT(version) ((version >> 8) & 0xFF)
#define GET_INSTRUCTION_SET(version) (version & 0xFF)

namespace sdm {

// Currently, we support only one client and one session for display core. So, create a global
// singleton core object.
struct CoreSingleton {
  CoreSingleton() : core_impl(NULL) { }

  CoreImpl *core_impl;
  Locker locker;
} g_core;

// TODO(user): Have a single structure handle carries all the interface pointers.
DisplayError CoreInterface::CreateCore(DebugHandler *debug_handler,
                                       BufferAllocator *buffer_allocator,
                                       BufferSyncHandler *buffer_sync_handler,
                                       CoreInterface **interface, uint32_t client_version) {
  return CreateCore(debug_handler, buffer_allocator, buffer_sync_handler, NULL,
                    interface, client_version);
}

DisplayError CoreInterface::CreateCore(DebugHandler *debug_handler,
                                       BufferAllocator *buffer_allocator,
                                       BufferSyncHandler *buffer_sync_handler,
                                       SocketHandler *socket_handler,
                                       CoreInterface **interface, uint32_t client_version) {
  SCOPE_LOCK(g_core.locker);

  if (!debug_handler || !buffer_allocator || !buffer_sync_handler || !interface) {
    return kErrorParameters;
  }

  // Check compatibility of client and core.
  uint32_t lib_version = SDM_VERSION_TAG;
  if (GET_REVISION(client_version) > GET_REVISION(lib_version)) {
    return kErrorVersion;
  } else if (GET_DATA_ALIGNMENT(client_version) != GET_DATA_ALIGNMENT(lib_version)) {
    return kErrorDataAlignment;
  } else if (GET_INSTRUCTION_SET(client_version) != GET_INSTRUCTION_SET(lib_version)) {
    return kErrorInstructionSet;
  }

  CoreImpl *&core_impl = g_core.core_impl;
  if (core_impl) {
    return kErrorUndefined;
  }

  Debug::SetDebugHandler(debug_handler);

  // Create appropriate CoreImpl object based on client version.
  if (GET_REVISION(client_version) == CoreImpl::kRevision) {
    core_impl = new CoreImpl(buffer_allocator, buffer_sync_handler, socket_handler);
  } else {
    return kErrorNotSupported;
  }

  if (!core_impl) {
    return kErrorMemory;
  }

  DisplayError error = core_impl->Init();
  if (error != kErrorNone) {
    delete core_impl;
    core_impl = NULL;
    return error;
  }

  *interface = core_impl;
  DLOGI("Open interface handle = %p", *interface);

  return kErrorNone;
}

DisplayError CoreInterface::DestroyCore() {
  SCOPE_LOCK(g_core.locker);

  DLOGI("Close handle");

  CoreImpl *&core_impl = g_core.core_impl;
  if (!core_impl) {
    return kErrorUndefined;
  }

  core_impl->Deinit();
  delete core_impl;
  core_impl = NULL;

  return kErrorNone;
}

}  // namespace sdm

