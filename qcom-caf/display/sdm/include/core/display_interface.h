/*
* Copyright (c) 2014 - 2018, 2020, The Linux Foundation. All rights reserved.
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

/*! @file display_interface.h
  @brief Interface file for display device which represents a physical panel or an output buffer
  where contents can be rendered.

  @details Display device is used to send layer buffers for composition and get them rendered onto
  the target device. Each display device represents a unique display target which may be either a
  physical panel or an output buffer..
*/
#ifndef __DISPLAY_INTERFACE_H__
#define __DISPLAY_INTERFACE_H__

#include <stdint.h>
#include <string>
#include <vector>
#include <utility>

#include "layer_stack.h"
#include "sdm_types.h"

namespace sdm {

typedef std::vector<std::pair<std::string, std::string>> AttrVal;

/*! @brief This enum represents display device types where contents can be rendered.

  @sa CoreInterface::CreateDisplay
  @sa CoreInterface::IsDisplaySupported
*/
enum DisplayType {
  kPrimary,         //!< Main physical display which is attached to the handheld device.
  kHDMI,            //!< HDMI physical display which is generally detachable.
  kVirtual,         //!< Contents would be rendered into the output buffer provided by the client
                    //!< e.g. wireless display.
  kDisplayMax,
};

/*! @brief This enum represents states of a display device.

  @sa DisplayInterface::GetDisplayState
  @sa DisplayInterface::SetDisplayState
*/
enum DisplayState {
  kStateOff,        //!< Display is OFF. Contents are not rendered in this state. Client will not
                    //!< receive VSync events in this state. This is default state as well.

  kStateOn,         //!< Display is ON. Contents are rendered in this state.

  kStateDoze,       //!< Display is ON and it is configured in a low power state.

  kStateDozeSuspend,
                    //!< Display is ON in a low power state and continue showing its current
                    //!< contents indefinitely until the mode changes.

  kStateStandby,    //!< Display is OFF. Client will continue to receive VSync events in this state
                    //!< if VSync is enabled. Contents are not rendered in this state.
};

/*! @brief This enum represents flags to override detail enhancer parameters.

  @sa DisplayInterface::SetDetailEnhancerData
*/
enum DetailEnhancerOverrideFlags {
  kOverrideDEEnable            = 0x1,     // Specifies to enable detail enhancer
  kOverrideDESharpen1          = 0x2,     // Specifies user defined Sharpening/smooth for noise
  kOverrideDESharpen2          = 0x4,     // Specifies user defined Sharpening/smooth for signal
  kOverrideDEClip              = 0x8,     // Specifies user defined DE clip shift
  kOverrideDELimit             = 0x10,    // Specifies user defined DE limit value
  kOverrideDEThrQuiet          = 0x20,    // Specifies user defined DE quiet threshold
  kOverrideDEThrDieout         = 0x40,    // Specifies user defined DE dieout threshold
  kOverrideDEThrLow            = 0x80,    // Specifies user defined DE low threshold
  kOverrideDEThrHigh           = 0x100,   // Specifies user defined DE high threshold
  kOverrideDEFilterConfig      = 0x200,   // Specifies user defined scaling filter config
  kOverrideDEMax               = 0xFFFFFFFF,
};

/*! @brief This enum represents Y/RGB scaling filter configuration.

  @sa DisplayInterface::SetDetailEnhancerData
*/
enum ScalingFilterConfig {
  kFilterEdgeDirected,
  kFilterCircular,
  kFilterSeparable,
  kFilterBilinear,
  kFilterMax,
};

/*! @brief This enum represents the quality level of the content.

  @sa DisplayInterface::SetDetailEnhancerData
*/
enum ContentQuality {
  kContentQualityUnknown,  // Default: high artifact and noise
  kContentQualityLow,      // Low quality content, high artifact and noise,
  kContentQualityMedium,   // Medium quality, medium artifact and noise,
  kContentQualityHigh,     // High quality content, low artifact and noise
  kContentQualityMax,
};

/*! @brief This enum represents the display port.

  @sa DisplayInterface::GetDisplayPort
*/
enum DisplayPort {
  kPortDefault,
  kPortDSI,        // Display is connected to DSI port.
  kPortDTV,        // Display is connected to DTV port
  kPortWriteBack,  // Display is connected to writeback port
  kPortLVDS,       // Display is connected to LVDS port
  kPortEDP,        // Display is connected to EDP port
  kPortDP,         // Display is connected to DP port.
};

/*! @brief This enum represents the events received by Display HAL. */
enum DisplayEvent {
  kIdleTimeout,    // Event triggered by Idle Timer.
  kThermalEvent,   // Event triggered by Thermal.
};

/*! @brief This enum represents the formats supported by Display

  kFormatYUV means only YUV.
  If both RGB and YUV are supported then resultant would be kFormatRGB | kFormatYUV.

*/
enum DisplayInterfaceFormat {
  kFormatNone                 = 0x0,
  kFormatRGB                  = 0x1,
  kFormatYUV                  = 0x2,
};

/*! @brief This structure defines configuration for fixed properties of a display device.

  @sa DisplayInterface::GetConfig
  @sa DisplayInterface::SetConfig
*/
struct DisplayConfigFixedInfo {
  bool underscan = false;   //!< If display support CE underscan.
  bool secure = false;      //!< If this display is capable of handling secure content.
  bool is_cmdmode = false;  //!< If panel is command mode panel.
  bool hdr_supported = false;  //!< if HDR is enabled
  uint32_t max_luminance = 0;  //!< From Panel's peak luminance
  uint32_t average_luminance = 0;  //!< From Panel's average luminance
  uint32_t min_luminance = 0;  //!< From Panel's blackness level
};

/*! @brief This structure defines configuration for variable properties of a display device.

  @sa DisplayInterface::GetConfig
  @sa DisplayInterface::SetConfig
*/
struct DisplayConfigVariableInfo {
  uint32_t x_pixels = 0;          //!< Total number of pixels in X-direction on the display panel.
  uint32_t y_pixels = 0;          //!< Total number of pixels in Y-direction on the display panel.
  float x_dpi = 0.0f;             //!< Dots per inch in X-direction.
  float y_dpi = 0.0f;             //!< Dots per inch in Y-direction.
  uint32_t fps = 0;               //!< Frame rate per second.
  uint32_t vsync_period_ns = 0;   //!< VSync period in nanoseconds.
  bool is_yuv = false;            //!< If the display output is in YUV format.
  uint32_t pixel_formats = 0;
};

/*! @brief Event data associated with VSync event.

  @sa DisplayEventHandler::VSync
*/
struct DisplayEventVSync {
  int64_t timestamp = 0;    //!< System monotonic clock timestamp in nanoseconds.
};

/*! @brief The structure defines the user input for detail enhancer module.

  @sa DisplayInterface::SetDetailEnhancerData
*/
struct DisplayDetailEnhancerData {
  uint32_t override_flags = 0;        // flags to specify which data to be set.
  uint16_t enable = 0;                // Detail enchancer enable
  int16_t sharpen_level1 = 0;         // Sharpening/smooth strenght for noise
  int16_t sharpen_level2 = 0;         // Sharpening/smooth strenght for signal
  uint16_t clip = 0;                  // DE clip shift
  uint16_t limit = 0;                 // DE limit value
  uint16_t thr_quiet = 0;             // DE quiet threshold
  uint16_t thr_dieout = 0;            // DE dieout threshold
  uint16_t thr_low = 0;               // DE low threshold
  uint16_t thr_high = 0;              // DE high threshold
  int32_t sharp_factor = 50;          // sharp_factor specifies sharpness/smoothness level,
                                      // range -100..100 positive for sharpness and negative for
                                      // smoothness
  ContentQuality quality_level = kContentQualityUnknown;
                                      // Specifies context quality level
  ScalingFilterConfig filter_config = kFilterEdgeDirected;
                                      // Y/RGB filter configuration
};

/*! @brief Display device event handler implemented by the client.

  @details This class declares prototype for display device event handler methods which must be
  implemented by the client. Display device will use these methods to notify events to the client.
  Client must post heavy-weight event handling to a separate thread and unblock display manager
  thread instantly.

  @sa CoreInterface::CreateDisplay
*/
class DisplayEventHandler {
 public:
  /*! @brief Event handler for VSync event.

    @details This event is dispatched on every vertical synchronization. The event is disabled by
    default.

    @param[in] vsync \link DisplayEventVSync \endlink

    @return \link DisplayError \endlink

    @sa DisplayInterface::GetDisplayState
    @sa DisplayInterface::SetDisplayState
  */
  virtual DisplayError VSync(const DisplayEventVSync &vsync) = 0;

  /*! @brief Event handler for Refresh event.

    @details This event is dispatched to trigger a screen refresh. Client must call Prepare() and
    Commit() in response to it from a separate thread. There is no data associated with this
    event.

    @return \link DisplayError \endlink

    @sa DisplayInterface::Prepare
    @sa DisplayInterface::Commit
  */
  virtual DisplayError Refresh() = 0;

  /*! @brief Event handler for CEC messages.

    @details This event is dispatched to send CEC messages to the CEC HAL.

    @param[in] message message to be sent

    @return \link DisplayError \endlink
  */
  virtual DisplayError CECMessage(char *message) = 0;

  /*! @brief Event handler for events received by Display HAL. */
  virtual DisplayError HandleEvent(DisplayEvent event) = 0;

 protected:
  virtual ~DisplayEventHandler() { }
};

struct PPDisplayAPIPayload;
struct PPPendingParams;

/*! @brief Display device interface.

  @details This class defines display device interface. It contains methods which client shall use
  to configure or submit layers for composition on the display device. This interface is created
  during display device creation and remains valid until destroyed.

  @sa CoreInterface::CreateDisplay
  @sa CoreInterface::DestroyDisplay
*/
class DisplayInterface {
 public:
  /*! @brief Method to determine hardware capability to compose layers associated with given frame.

    @details Client shall send all layers associated with a frame targeted for current display
    using this method and check the layers which can be handled completely in display manager.

    Client shall mark composition type for one of the layer as kCompositionGPUTarget; the GPU
    composed output would be rendered at the specified layer if some of the layers are not handled
    by SDM.

    Display manager will set each layer as kCompositionGPU or kCompositionSDE upon return. Client
    shall render all the layers marked as kCompositionGPU using GPU.

    This method can be called multiple times but only last call prevails. This method must be
    followed by Commit().

    @param[inout] layer_stack \link LayerStack \endlink

    @return \link DisplayError \endlink

    @sa Commit
  */
  virtual DisplayError Prepare(LayerStack *layer_stack) = 0;

  /*! @brief Method to commit layers of a frame submitted in a former call to Prepare().

    @details Client shall call this method to submit layers for final composition. The composed
    output would be displayed on the panel or written in output buffer.

    Client must ensure that layer stack is same as previous call to Prepare.

    This method shall be called only once for each frame.

    In the event of an error as well, this call will cause any fences returned in the previous call
    to Commit() to eventually become signaled, so the client's wait on fences can be released to
    prevent deadlocks.

    @param[in] layer_stack \link LayerStack \endlink

    @return \link DisplayError \endlink

    @sa Prepare
  */
  virtual DisplayError Commit(LayerStack *layer_stack) = 0;

  /*! @brief Method to flush any pending buffers/fences submitted previously via Commit() call.

    @details Client shall call this method to request the Display manager to release all buffers and
    respective fences currently in use. This operation may result in a blank display on the panel
    until a new frame is submitted for composition.

    @param[in] secure \link secure \endlink

    @return \link DisplayError \endlink

    @sa Prepare
    @sa Commit
  */
  virtual DisplayError Flush(bool secure) = 0;

  /*! @brief Method to get current state of the display device.

    @param[out] state \link DisplayState \endlink

    @return \link DisplayError \endlink

    @sa SetDisplayState
  */
  virtual DisplayError GetDisplayState(DisplayState *state) = 0;

  /*! @brief Method to get number of configurations(variable properties) supported on the display
    device.

    @param[out] count Number of modes supported; mode index starts with 0.

    @return \link DisplayError \endlink
  */
  virtual DisplayError GetNumVariableInfoConfigs(uint32_t *count) = 0;

  /*! @brief Method to get configuration for fixed properties of the display device.

    @param[out] fixed_info \link DisplayConfigFixedInfo \endlink

    @return \link DisplayError \endlink
  */
  virtual DisplayError GetConfig(DisplayConfigFixedInfo *fixed_info) = 0;

  /*! @brief Method to get configuration for variable properties of the display device.

    @param[in] index index of the mode
    @param[out] variable_info \link DisplayConfigVariableInfo \endlink

    @return \link DisplayError \endlink
  */
  virtual DisplayError GetConfig(uint32_t index, DisplayConfigVariableInfo *variable_info) = 0;

  /*! @brief Method to get index of active configuration of the display device.

    @param[out] index index of the mode corresponding to variable properties.

    @return \link DisplayError \endlink
  */
  virtual DisplayError GetActiveConfig(uint32_t *index) = 0;

  /*! @brief Method to get VSync event state. Default event state is disabled.

    @param[out] enabled vsync state

    @return \link DisplayError \endlink
  */
  virtual DisplayError GetVSyncState(bool *enabled) = 0;

  /*! @brief Method to set current state of the display device.

    @param[in] state \link DisplayState \endlink

    @return \link DisplayError \endlink

    @sa SetDisplayState
  */
  virtual DisplayError SetDisplayState(DisplayState state) = 0;

  /*! @brief Method to set active configuration for variable properties of the display device.

    @param[in] variable_info \link DisplayConfigVariableInfo \endlink

    @return \link DisplayError \endlink
  */
  virtual DisplayError SetActiveConfig(DisplayConfigVariableInfo *variable_info) = 0;

  /*! @brief Method to set active configuration for variable properties of the display device.

    @param[in] index index of the mode corresponding to variable properties.

    @return \link DisplayError \endlink
  */
  virtual DisplayError SetActiveConfig(uint32_t index) = 0;

  /*! @brief Method to set VSync event state. Default event state is disabled.

    @param[out] enabled vsync state

    @return \link DisplayError \endlink
  */
  virtual DisplayError SetVSyncState(bool enable) = 0;

  /*! @brief Method to set idle timeout value. Idle fallback is disabled with timeout value 0.

    @param[in] active_ms value in milliseconds.

    @return \link void \endlink
  */
  virtual void SetIdleTimeoutMs(uint32_t active_ms) = 0;

  /*! @brief Method to set maximum number of mixer stages for each display.

    @param[in] max_mixer_stages maximum number of mixer stages.

    @return \link DisplayError \endlink
  */
  virtual DisplayError SetMaxMixerStages(uint32_t max_mixer_stages) = 0;

  /*! @brief Method to control partial update feature for each display.

    @param[in] enable partial update feature control flag
    @param[out] pending whether the operation is completed or pending for completion

    @return \link DisplayError \endlink
  */
  virtual DisplayError ControlPartialUpdate(bool enable, uint32_t *pending) = 0;

  /*! @brief Method to disable partial update for at least 1 frame.
    @return \link DisplayError \endlink
  */
  virtual DisplayError DisablePartialUpdateOneFrame() = 0;

  /*! @brief Method to set the mode of the primary display.

    @param[in] mode the new display mode.

    @return \link DisplayError \endlink
  */
  virtual DisplayError SetDisplayMode(uint32_t mode) = 0;

  /*! @brief Method to get the min and max refresh rate of a display.

    @param[out] min and max refresh rate.

    @return \link DisplayError \endlink
  */
  virtual DisplayError GetRefreshRateRange(uint32_t *min_refresh_rate,
                                           uint32_t *max_refresh_rate) = 0;

  /*! @brief Method to set the refresh rate of a display.

    @param[in] new refresh rate of the display.

    @return \link DisplayError \endlink
  */
  virtual DisplayError SetRefreshRate(uint32_t refresh_rate) = 0;

  /*! @brief Method to query whether scanning is support for the HDMI display.

    @return \link DisplayError \endlink
  */
  virtual bool IsUnderscanSupported() = 0;

  /*! @brief Method to set brightness of the primary display.

    @param[in] level the new backlight level.

    @return \link DisplayError \endlink
  */
  virtual DisplayError SetPanelBrightness(int level) = 0;

  /*! @brief Method to notify display about change in min HDCP encryption level.

    @param[in] min_enc_level minimum encryption level value.

    @return \link DisplayError \endlink
  */
  virtual DisplayError OnMinHdcpEncryptionLevelChange(uint32_t min_enc_level) = 0;

  /*! @brief Method to route display API requests to color service.

    @param[in] in_payload \link PPDisplayAPIPayload \endlink
    @param[out] out_payload \link PPDisplayPayload \endlink
    @param[out] pending_action \link PPPendingParams \endlink

    @return \link DisplayError \endlink
  */
  virtual DisplayError ColorSVCRequestRoute(const PPDisplayAPIPayload &in_payload,
                                            PPDisplayAPIPayload *out_payload,
                                            PPPendingParams *pending_action) = 0;

  /*! @brief Method to request the number of color modes supported.

    @param[out] mode_count Number of modes

    @return \link DisplayError \endlink
  */
  virtual DisplayError GetColorModeCount(uint32_t *mode_count) = 0;

  /*! @brief Method to request the information of supported color modes.

    @param[inout] mode_count Number of updated modes
    @param[out] vector of mode strings

    @return \link DisplayError \endlink
  */
  virtual DisplayError GetColorModes(uint32_t *mode_count,
                                     std::vector<std::string> *color_modes) = 0;

  /*! @brief Method to request the attributes of color mode.

    @param[in] mode name
    @param[out] vector of mode attributes

    @return \link DisplayError \endlink
  */
  virtual DisplayError GetColorModeAttr(const std::string &color_mode,
                                        AttrVal *attr_map) = 0;

  /*! @brief Method to set the color mode

    @param[in] mode_name Mode name which needs to be set

    @return \link DisplayError \endlink
  */
  virtual DisplayError SetColorMode(const std::string &color_mode) = 0;

  /*! @brief Method to set the color transform

    @param[in] length Mode name which needs to be set
    @param[in] color_transform  4x4 Matrix for color transform

    @return \link DisplayError \endlink
  */
  virtual DisplayError SetColorTransform(const uint32_t length, const double *color_transform) = 0;

  /*! @brief Method to get the default color mode.

    @param[out] default mode name

    @return \link DisplayError \endlink
  */
  virtual DisplayError GetDefaultColorMode(std::string *color_mode) = 0;

  /*! @brief Method to request applying default display mode.

    @return \link DisplayError \endlink
  */
  virtual DisplayError ApplyDefaultDisplayMode() = 0;

  /*! @brief Method to set the position of the hw cursor.

    @param[in] x \link x position \endlink
    @param[in] y \link y position \endlink

    @return \link DisplayError \endlink
  */
  virtual DisplayError SetCursorPosition(int x, int y) = 0;

  /*! @brief Method to get the brightness level of the display

    @param[out] level brightness level

    @return \link DisplayError \endlink
  */
  virtual DisplayError GetPanelBrightness(int *level) = 0;

  /*! @brief Method to set layer mixer resolution.

    @param[in] width layer mixer width
    @param[in] height layer mixer height

    @return \link DisplayError \endlink
  */
  virtual DisplayError SetMixerResolution(uint32_t width, uint32_t height) = 0;

  /*! @brief Method to get layer mixer resolution.

    @param[out] width layer mixer width
    @param[out] height layer mixer height

    @return \link DisplayError \endlink
  */
  virtual DisplayError GetMixerResolution(uint32_t *width, uint32_t *height) = 0;

  /*! @brief Method to set  frame buffer configuration.

    @param[in] variable_info \link DisplayConfigVariableInfo \endlink

    @return \link DisplayError \endlink
  */
  virtual DisplayError SetFrameBufferConfig(const DisplayConfigVariableInfo &variable_info) = 0;

  /*! @brief Method to get frame buffer configuration.

    @param[out] variable_info \link DisplayConfigVariableInfo \endlink

    @return \link DisplayError \endlink
  */
  virtual DisplayError GetFrameBufferConfig(DisplayConfigVariableInfo *variable_info) = 0;

  /*! @brief Method to set detail enhancement data.

    @param[in] de_data \link DisplayDetailEnhancerData \endlink

    @return \link DisplayError \endlink
  */
  virtual DisplayError SetDetailEnhancerData(const DisplayDetailEnhancerData &de_data) = 0;

  /*! @brief Method to get display port information.

    @param[out] port \link DisplayPort \endlink

    @return \link DisplayError \endlink
  */
  virtual DisplayError GetDisplayPort(DisplayPort *port) = 0;

  /*! @brief Method to query whether it is Primrary device.

    @return true if this interface is primary.
  */
  virtual bool IsPrimaryDisplay() = 0;

  /*! @brief Method to toggle composition types handling by SDM.

    @details Client shall call this method to request SDM to enable/disable a specific type of
    layer composition. If client disables a composition type, SDM will not handle any of the layer
    composition using the disabled method in a draw cycle. On lack of resources to handle all
    layers using other enabled composition methods, Prepare() will return an error.

    Request to toggle composition type is applied from subsequent draw cycles.

    Default state of all defined composition types is enabled.

    @param[in] composition_type \link LayerComposition \endlink
    @param[in] enable \link enable composition type \endlink

    @return \link DisplayError \endlink

    @sa Prepare
  */
  virtual DisplayError SetCompositionState(LayerComposition composition_type, bool enable) = 0;

  /*! @brief Method to check whether a client target with the given properties
      can be supported/handled by hardware.

    @param[in] width client target width
    @param[in] height client target height
    @param[in] format client target format
    @param[in] colorMetaData client target colorMetaData

    @return \link DisplayError \endlink
  */
  virtual DisplayError GetClientTargetSupport(uint32_t width, uint32_t height,
                                              LayerBufferFormat format,
                                              const ColorMetaData &color_metadata) = 0;

  /*! @brief Method to dynamically set DSI clock rate.

      @param[in] bitclk DSI bit clock in HZ.

      @return \link DisplayError \endlink
  */
  virtual DisplayError SetDynamicDSIClock(uint64_t bitclk) = 0;

  /*! @brief Method to get the current DSI clock rate

      @param[out] bitclk DSI bit clock in HZ

      @return \link DisplayError \endlink
  */
  virtual DisplayError GetDynamicDSIClock(uint64_t *bitclk) = 0;

  /*! @brief Method to get the supported DSI clock rates

      @param[out] bitclk DSI bit clock in HZ

      @return \link DisplayError \endlink
  */
  virtual DisplayError GetSupportedDSIClock(std::vector<uint64_t> *bitclk_rates) = 0;

  /*
   * Returns a string consisting of a dump of SDM's display and layer related state
   * as programmed to driver
  */
  virtual std::string Dump() = 0;

 protected:
  virtual ~DisplayInterface() { }
};

}  // namespace sdm

#endif  // __DISPLAY_INTERFACE_H__

