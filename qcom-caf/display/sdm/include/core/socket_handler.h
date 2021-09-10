/*
* Copyright (c) 2016, The Linux Foundation. All rights reserved.
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

/*! @file socket_handler.h
  @brief Interface file for platform specific Socket Handler.

  @details SDM will use this interface to get the platform specific Socket fd.
*/

#ifndef __SOCKET_HANDLER_H__
#define __SOCKET_HANDLER_H__

namespace sdm {

/*! @brief This enum represents Socket types, for which SDM can request the fd.

*/
enum SocketType {
  kDpps,       //!< Socket for Dpps
};

/*! @brief Socket handler implemented by the client

  @details This class declares prototype for SocketHandler methods which must be
  implemented by client. SDM will use these methods to get the platform specific Socket fd.

  @sa CoreInterface::CreateCore
*/
class SocketHandler {
 public:
  /*! @brief Method to get the platform specific Socket fd for a given socket type.

    @details This method returns the platform specific Socket fd for a given socket type.
    It is the responsibility of the caller to close the file descriptor.

    @param[in] socket_type

    @return \link int \endlink
  */

  virtual int GetSocketFd(SocketType socket_type) = 0;

 protected:
  virtual ~SocketHandler() { }
};

}  // namespace sdm

#endif  // __SOCKET_HANDLER_H__
