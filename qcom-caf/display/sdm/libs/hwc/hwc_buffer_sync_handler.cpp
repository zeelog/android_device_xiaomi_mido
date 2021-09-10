/*
* Copyright (c) 2015, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*  * Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
*  * Redistributions in binary form must reproduce the above
*    copyright notice, this list of conditions and the following
*    disclaimer in the documentation and/or other materials provided
*    with the distribution.
*  * Neither the name of The Linux Foundation nor the names of its
*    contributors may be used to endorse or promote products derived
*    from this software without specific prior written permission.
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
#include <sync/sync.h>
#include <utils/constants.h>
#include <utils/debug.h>

#include "hwc_debugger.h"
#include "hwc_buffer_sync_handler.h"

#define __CLASS__ "HWCBufferSyncHandler"

namespace sdm {

DisplayError HWCBufferSyncHandler::SyncWait(int fd) {
  int error = 0;

  if (fd >= 0) {
    error = sync_wait(fd, 1000);
    if (error < 0) {
      DLOGE("sync_wait error errno = %d, desc = %s", errno,  strerror(errno));
      return kErrorTimeOut;
    }
  }

  return kErrorNone;
}

DisplayError HWCBufferSyncHandler::SyncMerge(int fd1, int fd2, int *merged_fd) {
  DisplayError error = kErrorNone;

  // Merge the two fences.  In the case where one of the fences is not a
  // valid fence (e.g. NO_FENCE) merge the one valid fence with itself so
  // that a new fence with the given name is created.
  // TODO(user): "SyncMerge"string should be replaced with user-defined string to represent
  // why it is merged.
  if (fd1 >= 0 && fd2 >= 0) {
    *merged_fd = sync_merge("SyncMerge", fd1, fd2);
  } else if (fd1 >= 0) {
    *merged_fd = sync_merge("SyncMerge", fd1, fd1);
  } else if (fd2 >= 0) {
    *merged_fd = sync_merge("SyncMerge", fd2, fd2);
  } else {
    *merged_fd = -1;
    return kErrorNone;
  }

  if (*merged_fd == -1) {
    DLOGE("Sync merge error! fd1 %d fd2 %d", fd1, fd2);
    error = kErrorFileDescriptor;
  }

  return error;
}

bool HWCBufferSyncHandler::IsSyncSignaled(int fd) {
  if (sync_wait(fd, 0) < 0) {
    return false;
  } else {
    return true;
  }
}

}  // namespace sdm

