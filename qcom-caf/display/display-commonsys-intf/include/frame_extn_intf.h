/*
* Copyright (c) 2019, The Linux Foundation. All rights reserved.
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

#ifndef __FRAME_EXTN_INTF_H__
#define __FRAME_EXTN_INTF_H__

#include <utils/Timers.h>
#include <sys/types.h>
#include <string>

#define EXTENSION_LIBRARY_NAME "libframeextension.so"
#define CREATE_FRAME_EXTN_INTERFACE "CreateFrameExtnInterface"
#define DESTROY_FRAME_EXTN_INTERFACE "DestroyFrameExtnInterface"

namespace composer {

class FrameExtnIntf;

// Function addresses queried at runtime using ::dlsym()
typedef bool (*CreateFrameExtnInterface)(FrameExtnIntf **interface);
typedef bool (*DestroyFrameExtnInterface)(FrameExtnIntf *interface);

/*! @brief This structure defines extension version.

    @details It is used to avoid any mismatch of versions between frameextension library
    implementation and its clients usage (like SurfaceFlinger).

  @sa FrameInfo
*/
struct Version {
  uint8_t minor;
  uint8_t major;
};

/*! @brief This structure defines the Frame info required by FrameExtnIntf.

  @sa FrameExtnIntf::SetFrameInfo
*/
struct FrameInfo {
  Version version;
  bool transparent_region;
  int width;
  int height;
  int max_queued_frames;
  int num_idle;
  std::string max_queued_layer_name;
  std::string layer_name;
  nsecs_t current_timestamp;
  nsecs_t previous_timestamp;
  nsecs_t vsync_timestamp;
  nsecs_t refresh_timestamp;
  nsecs_t ref_latency;
  nsecs_t vsync_period;
};


/*! @brief This interface shall be implemented by frameextension library.

  @details This class declares prototype for frameextension public interfaces which must be
  implemented by frameextension library.
*/
class FrameExtnIntf {
 public:
  /*! @brief Set the FrameInfo used by frameextension.

    @details This function is called once per refresh cycle so that required frame info are
    feed to frameextension.

    @param[in] frameInfo \link FrameInfo \endlink

    @return \link int \endlink
  */
  virtual int SetFrameInfo(FrameInfo &frameInfo) = 0;

 protected:
  virtual ~FrameExtnIntf() { };
};

}  // namespace composer

#endif  // __FRAME_EXTN_INTF_H__
