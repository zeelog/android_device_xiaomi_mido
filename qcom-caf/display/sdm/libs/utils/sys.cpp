/*
* Copyright (c) 2015, The Linux Foundation. All rights reserved.
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

#include <utils/sys.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <string>

#define __CLASS__ "Sys"

namespace sdm {

#ifndef SDM_VIRTUAL_DRIVER

int PthreadCancel(pthread_t /* thread */) {
  return 0;
}

// Pointer to actual driver interfaces.
Sys::ioctl Sys::ioctl_ = ::ioctl;
Sys::access Sys::access_ = ::access;
Sys::open Sys::open_ = ::open;
Sys::close Sys::close_ = ::close;
Sys::poll Sys::poll_ = ::poll;
Sys::pread Sys::pread_ = ::pread;
Sys::pwrite Sys::pwrite_ = ::pwrite;
Sys::pthread_cancel Sys::pthread_cancel_ = PthreadCancel;
Sys::dup Sys::dup_ = ::dup;
Sys::read Sys::read_ = ::read;
Sys::write Sys::write_ = ::write;
Sys::eventfd Sys::eventfd_ = ::eventfd;

bool Sys::getline_(fstream &fs, std::string &line) {
  return std::getline(fs, line) ? true : false;
}

#endif  // SDM_VIRTUAL_DRIVER

DynLib::~DynLib() {
  Close();
}

bool DynLib::Open(const char *lib_name) {
  Close();
  lib_ = ::dlopen(lib_name, RTLD_NOW);

  return (lib_ != NULL);
}

bool DynLib::Sym(const char *func_name, void **func_ptr) {
  if (lib_) {
    *func_ptr = ::dlsym(lib_, func_name);
  } else {
    *func_ptr = NULL;
  }

  return (*func_ptr != NULL);
}

void DynLib::Close() {
  if (lib_) {
    ::dlclose(lib_);
    lib_ = NULL;
  }
}

}  // namespace sdm

