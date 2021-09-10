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

/*! @file smomo.h
  @brief Interface file for SmoMo which defines the public interface exposed to SmoMo clients.

  @details SmoMo clients use these interfaces to feed SmoMo required info like frame rate, refresh
  rate, these info are used to update SmoMo internal state.
*/

#ifndef __SMOMO_INTERFACE_H__
#define __SMOMO_INTERFACE_H__

#include <sys/types.h>

#include <vector>
#include <string>

namespace smomo {

#define SMOMO_LIBRARY_NAME "libsmomo.qti.so"
#define CREATE_SMOMO_INTERFACE_NAME "CreateSmomoInterface"
#define DESTROY_SMOMO_INTERFACE_NAME "DestroySmomoInterface"

#define SMOMO_REVISION_MAJOR (1)
#define SMOMO_REVISION_MINOR (0)
#define SMOMO_VERSION_TAG ((uint16_t) ((SMOMO_REVISION_MAJOR << 8) \
                                          | SMOMO_REVISION_MINOR))

typedef int64_t nsecs_t;

/*! @brief This structure defines the layer stats required by SmoMo.

  @sa SmomoIntf::updateSmomoState
*/
struct SmomoLayerStats {
  std::string name;  // layer full name
  int32_t id;  // layer ID
};

/*! @brief This structure defines the buffer stats required by SmoMo.

  @sa SmomoIntf::CollectLayerStats
  @sa SmomoIntf::ShouldPresentNow
*/
struct SmomoBufferStats {
  int32_t id;  // layer ID
  int32_t queued_frames;  // queued frame count of this layer
  bool auto_timestamp;  // whether timestamp was generated automatically
  nsecs_t timestamp;  // layer buffer's timestamp
  nsecs_t dequeue_latency;  // last dequeue duration
};

/*! @brief SmoMo interface implemented by SmoMo library.

  @details This class declares prototype for SmoMo public interfaces which must be
  implemented by SmoMo library. SmoMo clients will use these methods to feed required
  info to SmoMo implementation.
*/
class SmomoIntf {
 public:
  virtual ~SmomoIntf() = default;

  /*! @brief Update SmoMo internal state.

    @details This function is called once per each composition so that required layer info are feed
    to SmoMo, the SmoMo uses these info to update its internal state.

    @param[in] layers \link SmomoLayerStats \endlink
    @param[in] refresh_rate current display refresh rate

    @return \link void \endlink
  */
  virtual void UpdateSmomoState(const std::vector<SmomoLayerStats> &layers,
      float refresh_rate) = 0;

  /*! @brief Collect layer buffer stats.

    @details This function is called once new buffer is ready for this layer. It's used to collect
    layer's buffer stats for SmoMo.

    @param[in] \link SmomoBufferStats \endlink

    @return \link void \endlink
  */
  virtual void CollectLayerStats(const SmomoBufferStats &buffer_stats) = 0;

  /*! @brief Is this layer buffer ready to present.

    @details This function is called by SmoMo clients used to check whether this layer buffer is
    due to present.

    @param[in] \link SmomoBufferStats \endlink
    @param[in] next_vsync_time When next VSYNC arrives

    @return \link bool \endlink
  */
  virtual bool ShouldPresentNow(const SmomoBufferStats &buffer_stats,
      const nsecs_t next_vsync_time) = 0;

  /*! @brief Change refresh rate callback type definition.
  */
  using ChangeRefreshRateCallback = std::function<void(int32_t)>;

  /*! @brief Set the callback used by SmoMo to change display refresh rate.

    @details This function is called by SmoMo clients used to set the refresh rate callback.

    @param[in] callback \link ChangeRefreshRateCallback \endlink

    @return \link void \endlink
  */
  virtual void SetChangeRefreshRateCallback(
      const ChangeRefreshRateCallback& callback) = 0;

  /*! @brief Set the refersh rates supported by display.

    @details This function is called to tell SmoMo what refresh rates this display can suport.

    @param[in] refresh_rates The refresh rates supported by the display

    @return \link void \endlink
  */
  virtual void SetDisplayRefreshRates(const std::vector<float> &refresh_rates) = 0;

  /*! @brief Get the current frame rate from SmoMo.

    @details This function is called by SmoMo client to query the current frame rate, which is
    based on the internal state of SmoMo. Client needs to call the UpdateSmomoState API before
    calling this function. SmoMo only returns a valid frame rate when it's settled to a state.

    @return > 0 if valid, -1 if invalid.

    @return \link int \endlink
  */
  virtual int GetFrameRate() = 0;
};

typedef bool (*CreateSmomoInterface)(uint16_t version, SmomoIntf **interface);
typedef void (*DestroySmomoInterface)(SmomoIntf *interface);

}  // namespace smomo

#endif  // __SMOMO_INTERFACE_H__
