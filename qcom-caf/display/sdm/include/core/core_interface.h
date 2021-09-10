/*
* Copyright (c) 2014 - 2016, 2018 The Linux Foundation. All rights reserved.
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

/*! @file core_interface.h
  @brief Interface file for core of the display subsystem.

  @details Display core is primarily used for loading and unloading different display device
  components viz primary, external and virtual. Display core is a statically linked library which
  runs in caller's process context.
*/
#ifndef __CORE_INTERFACE_H__
#define __CORE_INTERFACE_H__

#include <stdint.h>

#include "display_interface.h"
#include "sdm_types.h"
#include "buffer_allocator.h"
#include "buffer_sync_handler.h"
#include "socket_handler.h"

/*! @brief Display manager interface version.

  @details Display manager interfaces are version tagged to maintain backward compatibility. This
  version is supplied as a default argument during display core initialization.

  Client may use an older version of interfaces and link to a higher version of display manager
  library, but vice versa is not allowed.

  A 32-bit client must use 32-bit display core library and a 64-bit client must use 64-bit display
  core library.

  Display manager interfaces follow default data structures alignment. Client must not override the
  default padding rules while using these interfaces.

  @warning It is assumed that client upgrades or downgrades display core interface all at once
  and recompile all binaries which use these interfaces. Mix and match of these interfaces can
  lead to unpredictable behaviour.

  @sa CoreInterface::CreateCore
*/
#define SDM_REVISION_MAJOR (1)
#define SDM_REVISION_MINOR (0)

#define SDM_VERSION_TAG ((uint32_t) ((SDM_REVISION_MAJOR << 24) | (SDM_REVISION_MINOR << 16) | \
                                    (sizeof(SDMCompatibility) << 8) | sizeof(int *)))

namespace sdm {

/*! @brief Forward declaration for debug handler.
*/
class DebugHandler;

/*! @brief This enum represents max bandwidth limit mode.

  @sa DisplayInterface::SetMaxBandwidthMode
*/
enum HWBwModes {
  kBwDefault,      //!< Default state. No change in device bandwidth limit.
  kBwCamera,       //!< Camera is on. Bandwidth limit should be reduced accordingly.
  kBwVFlip,        //!< VFlip is required. Reduce bandwidth limit accordingly.
  kBwHFlip,        //!< HFlip is required.  Reduce bandwidth limit accordingly.
  kBwModeMax,      //!< Limiter for maximum available bandwidth modes.
};


/*! @brief Information on hardware for the first display

  @details This structure returns the display type of the first display on the device
  (internal display or HDMI etc) and whether it is currently connected,

*/
struct HWDisplayInterfaceInfo {
  DisplayType type;
  bool is_connected;
};

/*! @brief Display core interface.

  @details This class defines display core interfaces. It contains methods which client shall use
  to create/destroy different display devices. This interface is created during display core
  CreateCore() and remains valid until DestroyCore().

  @sa CoreInterface::CreateCore
  @sa CoreInterface::DestroyCore
*/
class CoreInterface {
 public:
  /*! @brief Method to create and get handle to display core interface.

    @details This method is the entry point into the display core. Client can create and operate on
    different display devices only through a valid interface handle obtained using this method. An
    object of display core is created and handle to this object is returned via output parameter.
    This interface shall be called only once.

    @param[in] debug_handler \link DebugHandler \endlink
    @param[in] buffer_allocator \link BufferAllocator \endlink
    @param[in] buffer_sync_handler \link BufferSyncHandler \endlink
    @param[out] interface \link CoreInterface \endlink
    @param[in] version \link SDM_VERSION_TAG \endlink. Client must not override this argument.

    @return \link DisplayError \endlink

    @sa DestroyCore
  */
  static DisplayError CreateCore(DebugHandler *debug_handler, BufferAllocator *buffer_allocator,
                                 BufferSyncHandler *buffer_sync_handler, CoreInterface **interface,
                                 uint32_t version = SDM_VERSION_TAG);

  /*! @brief Method to create and get handle to display core interface.

    @details This method is the entry point into the display core. Client can create and operate on
    different display devices only through a valid interface handle obtained using this method. An
    object of display core is created and handle to this object is returned via output parameter.
    This interface shall be called only once.

    @param[in] debug_handler \link DebugHandler \endlink
    @param[in] buffer_allocator \link BufferAllocator \endlink
    @param[in] buffer_sync_handler \link BufferSyncHandler \endlink
    @param[in] socket_handler \link SocketHandler \endlink
    @param[out] interface \link CoreInterface \endlink
    @param[in] version \link SDM_VERSION_TAG \endlink. Client must not override this argument.

    @return \link DisplayError \endlink

    @sa DestroyCore
  */
  static DisplayError CreateCore(DebugHandler *debug_handler, BufferAllocator *buffer_allocator,
                                 BufferSyncHandler *buffer_sync_handler,
                                 SocketHandler *socket_handler, CoreInterface **interface,
                                 uint32_t version = SDM_VERSION_TAG);

  /*! @brief Method to release handle to display core interface.

    @details The object of corresponding display core is destroyed when this method is invoked.
    Client must explicitly destroy all created display device objects associated with this handle
    before invoking this method.

    @param[in] interface \link CoreInterface \endlink

    @return \link DisplayError \endlink

    @sa CreateCore
  */
  static DisplayError DestroyCore();

  /*! @brief Method to create a display device for a given type.

    @details Client shall use this method to create each of the connected display type. A handle to
    interface associated with this object is returned via output parameter which can be used to
    interact further with the display device.

    @param[in] type \link DisplayType \endlink
    @param[in] event_handler \link DisplayEventHandler \endlink
    @param[out] interface \link DisplayInterface \endlink

    @return \link DisplayError \endlink

    @sa DestroyDisplay
  */
  virtual DisplayError CreateDisplay(DisplayType type, DisplayEventHandler *event_handler,
                                     DisplayInterface **interface) = 0;

  /*! @brief Method to destroy a display device.

    @details Client shall use this method to destroy each of the created display device objects.

    @param[in] interface \link DisplayInterface \endlink

    @return \link DisplayError \endlink

    @sa CreateDisplay
  */
  virtual DisplayError DestroyDisplay(DisplayInterface *interface) = 0;

  /*! @brief Method to update the bandwidth limit as per given mode.

    @param[in] mode indicate the mode or use case

    @return \link DisplayError \endlink

   */
    virtual DisplayError SetMaxBandwidthMode(HWBwModes mode) = 0;

  /*! @brief Method to get characteristics of the first display.

    @details Client shall use this method to determine if the first display is HDMI, and whether
    it is currently connected.

    @param[in] hw_disp_info structure that this method will fill up with info.

    @return \link DisplayError \endlink

   */
    virtual DisplayError GetFirstDisplayInterfaceType(HWDisplayInterfaceInfo *hw_disp_info) = 0;

   /*! @brief Method to check color transform supported or not.

    @details Client shall use this method to determine if target supports color transform or not

    @return bool

   */
    virtual bool IsColorTransformSupported() = 0;


 protected:
  virtual ~CoreInterface() { }
};

}  // namespace sdm

#endif  // __CORE_INTERFACE_H__

