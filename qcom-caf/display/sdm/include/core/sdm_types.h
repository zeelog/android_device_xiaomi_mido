/*
* Copyright (c) 2014 - 2017, The Linux Foundation. All rights reserved.
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

/*! @file sdm_types.h
  @brief This file contains miscellaneous data types used across display interfaces.
*/
#ifndef __SDM_TYPES_H__
#define __SDM_TYPES_H__

namespace sdm {

/*! @brief This enum represents different error codes that display interfaces may return.
*/
enum DisplayError {
  kErrorNone,             //!< Call executed successfully.
  kErrorUndefined,        //!< An unspecified error has occured.
  kErrorNotSupported,     //!< Requested operation is not supported.
  kErrorPermission,       //!< Operation is not permitted in current state.
  kErrorVersion,          //!< Client is using advanced version of interfaces and calling into an
                          //!< older version of display library.
  kErrorDataAlignment,    //!< Client data structures are not aligned on naturual boundaries.
  kErrorInstructionSet,   //!< 32-bit client is calling into 64-bit library or vice versa.
  kErrorParameters,       //!< Invalid parameters passed to a method.
  kErrorFileDescriptor,   //!< Invalid file descriptor.
  kErrorMemory,           //!< System is running low on memory.
  kErrorResources,        //!< Not enough hardware resources available to execute call.
  kErrorHardware,         //!< A hardware error has occured.
  kErrorTimeOut,          //!< The operation has timed out to prevent client from waiting forever.
  kErrorShutDown,         //!< Driver is processing shutdown sequence
  kErrorPerfValidation,   //!< Bandwidth or Clock requirement validation failure.
  kErrorNoAppLayers,      //!< No App layer(s) in the draw cycle.
  kErrorNotValidated,     //!< Draw cycle has not been validated.
};

/*! @brief This structure is defined for client and library compatibility check purpose only. This
  structure is used in SDM_VERSION_TAG definition only. Client should not refer it directly for
  any purpose.
*/
struct SDMCompatibility {
  char c1;
  int i1;
  char c2;
  int i2;
};

}  // namespace sdm

#endif  // __SDM_TYPES_H__

