/*
* Copyright (c) 2015 - 2016, The Linux Foundation. All rights reserved.
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

/*! @file debug_interface.h
  @brief This file provides the debug interface for display manager.
*/
#ifndef __DEBUG_INTERFACE_H__
#define __DEBUG_INTERFACE_H__

namespace sdm {

/*! @brief This enum represents different modules/logical unit tags that a log message may
  be associated with. Client may use this to filter messages for dynamic logging.

  @sa DebugHandler
*/
enum DebugTag {
  kTagNone,             //!< Debug log is not tagged. This type of logs should always be printed.
  kTagResources,        //!< Debug log is tagged for resource management.
  kTagStrategy,         //!< Debug log is tagged for strategy decisions.
  kTagCompManager,      //!< Debug log is tagged for composition manager.
  kTagDriverConfig,     //!< Debug log is tagged for driver config.
  kTagRotator,          //!< Debug log is tagged for rotator.
  kTagScalar,           //!< Debug log is tagged for Scalar Helper.
  kTagQDCM,             //!< Debug log is tagged for display QDCM color managing.
  kTagDisplay,          //!< Debug log is tagged for display core logs.
  kTagClient,           //!< Debug log is tagged for SDM client.
};

/*! @brief Display debug handler class.

  @details This class defines display debug handler. The handle contains methods which client
  should implement to get different levels of logging/tracing from display manager. Display manager
   will call into these methods at appropriate times to send logging/tracing information.

  @sa CoreInterface::CreateCore
*/
class DebugHandler {
 public:
  /*! @brief Method to handle error messages.

    @param[in] tag \link DebugTag \endlink
    @param[in] format \link message format with variable argument list \endlink
  */
  virtual void Error(DebugTag tag, const char *format, ...) = 0;

  /*! @brief Method to handle warning messages.

    @param[in] tag \link DebugTag \endlink
    @param[in] format \link message format with variable argument list \endlink
  */
  virtual void Warning(DebugTag tag, const char *format, ...) = 0;

  /*! @brief Method to handle informative messages.

    @param[in] tag \link DebugTag \endlink
    @param[in] format \link message format with variable argument list \endlink
  */
  virtual void Info(DebugTag tag, const char *format, ...) = 0;

  /*! @brief Method to handle debug messages.

    @param[in] tag \link DebugTag \endlink
    @param[in] format \link message format with variable argument list \endlink
  */
  virtual void Debug(DebugTag tag, const char *format, ...) = 0;

  /*! @brief Method to handle verbose messages.

    @param[in] tag \link DebugTag \endlink
    @param[in] format \link message format with variable argument list \endlink
  */
  virtual void Verbose(DebugTag tag, const char *format, ...) = 0;

  /*! @brief Method to begin trace for a module/logical unit.

    @param[in] class_name \link name of the class that the function belongs to \endlink
    @param[in] function_name \link name of the function to be traced \endlink
    @param[in] custom_string \link custom string for multiple traces within a function \endlink
  */
  virtual void BeginTrace(const char *class_name, const char *function_name,
                          const char *custom_string) = 0;

  /*! @brief Method to end trace for a module/logical unit.
  */
  virtual void EndTrace() = 0;

  /*! @brief Method to get property value corresponding to give string.

    @param[in] property_name name of the property
    @param[out] integer converted value corresponding to the property name

    @return \link DisplayError \endlink
  */
  virtual DisplayError GetProperty(const char *property_name, int *value) = 0;

  /*! @brief Method to get property value corresponding to give string.

   @param[in] property_name name of the property
   @param[out] string value corresponding to the property name

   @return \link DisplayError \endlink
  */
  virtual DisplayError GetProperty(const char *property_name, char *value) = 0;

  /*! @brief Method to set a property to a given string value.

   @param[in] property_name name of the property
   @param[in] value new value of the property name

   @return \link DisplayError \endlink
  */
  virtual DisplayError SetProperty(const char *property_name, const char *value) = 0;

 protected:
  virtual ~DebugHandler() { }
};

/*! @brief Scope tracer template class.

  @details This class template implements the funtionality to capture the trace for function/
  module. It starts the trace upon object creation and ends the trace upon object destruction.
*/
template <class T>
class ScopeTracer {
 public:
  ScopeTracer(const char *class_name, const char *function_name) {
    T::Get()->BeginTrace(class_name, function_name, "");
  }

  ~ScopeTracer() { T::Get()->EndTrace(); }
};

}  // namespace sdm

#endif  // __DEBUG_INTERFACE_H__



