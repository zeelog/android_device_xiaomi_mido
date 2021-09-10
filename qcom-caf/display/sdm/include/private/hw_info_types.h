/*
* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
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

#ifndef __HW_INFO_TYPES_H__
#define __HW_INFO_TYPES_H__

#include <stdint.h>
#include <core/display_interface.h>
#include <core/core_interface.h>
#include <vector>
#include <map>
#include <string>
#include <bitset>

namespace sdm {
using std::string;

const int kMaxSDELayers = 16;   // Maximum number of layers that can be handled by MDP5 hardware
                                // in a given layer stack.
#define MAX_PLANES 4

#define MAX_DETAIL_ENHANCE_CURVE 3

enum HWDeviceType {
  kDevicePrimary,
  kDeviceHDMI,
  kDeviceVirtual,
  kDeviceRotator,
  kDeviceMax,
};

enum HWBlockType {
  kHWPrimary,
  kHWHDMI,
  kHWWriteback0,
  kHWWriteback1,
  kHWWriteback2,
  kHWBlockMax
};

enum HWDisplayMode {
  kModeDefault,
  kModeVideo,
  kModeCommand,
};

enum PipeType {
  kPipeTypeUnused,
  kPipeTypeVIG,
  kPipeTypeRGB,
  kPipeTypeDMA,
  kPipeTypeCursor,
};

enum HWSubBlockType {
  kHWVIGPipe,
  kHWRGBPipe,
  kHWDMAPipe,
  kHWCursorPipe,
  kHWRotatorInput,
  kHWRotatorOutput,
  kHWWBIntfOutput,
  kHWDestinationScalar,
  kHWSubBlockMax,
};

enum HWAlphaInterpolation {
  kInterpolationPixelRepeat,
  kInterpolationBilinear,
  kInterpolationMax,
};

enum HWBlendingFilter {
  kBlendFilterCircular,
  kBlendFilterSeparable,
  kBlendFilterMax,
};

enum HWPipeFlags {
  kIGC = 0x01,
  kMultiRect = 0x02,
  kMultiRectParallelMode = 0x04,
};

enum HWAVRModes {
  kContinuousMode,  // Mode to enable AVR feature for every frame.
  kOneShotMode,     // Mode to enable AVR feature for particular frame.
};

typedef std::map<HWSubBlockType, std::vector<LayerBufferFormat>> FormatsMap;

struct HWDynBwLimitInfo {
  uint32_t cur_mode = kBwDefault;
  uint32_t total_bw_limit[kBwModeMax] = { 0 };
  uint32_t pipe_bw_limit[kBwModeMax] = { 0 };
};

struct HWPipeCaps {
  PipeType type = kPipeTypeUnused;
  uint32_t id = 0;
  uint32_t master_pipe_id = 0;
  uint32_t max_rects = 1;
};

struct HWRotatorInfo {
  enum { ROT_TYPE_MDSS, ROT_TYPE_V4L2 };
  uint32_t type = ROT_TYPE_MDSS;
  uint32_t num_rotator = 0;
  bool has_downscale = false;
  std::string device_path = "";
  float min_downscale = 2.0f;
  bool downscale_compression = false;
};

struct HWDestScalarInfo {
  uint32_t count = 0;
  uint32_t max_input_width = 0;
  uint32_t max_output_width = 0;
  uint32_t max_scale_up = 1;
};

enum SmartDMARevision {
  V1,
  V2,
};

struct HWResourceInfo {
  uint32_t hw_version = 0;
  uint32_t hw_revision = 0;
  uint32_t num_dma_pipe = 0;
  uint32_t num_vig_pipe = 0;
  uint32_t num_rgb_pipe = 0;
  uint32_t num_cursor_pipe = 0;
  uint32_t num_blending_stages = 0;
  uint32_t num_control = 0;
  uint32_t num_mixer_to_disp = 0;
  uint32_t smp_total = 0;
  uint32_t smp_size = 0;
  uint32_t num_smp_per_pipe = 0;
  uint32_t max_scale_up = 1;
  uint32_t max_scale_down = 1;
  float rot_downscale_max = 0.0f;
  uint64_t max_bandwidth_low = 0;
  uint64_t max_bandwidth_high = 0;
  uint32_t max_mixer_width = 2048;
  uint32_t max_pipe_width = 2048;
  uint32_t max_cursor_size = 0;
  uint32_t max_pipe_bw =  0;
  uint32_t max_sde_clk = 0;
  float clk_fudge_factor = 1.0f;
  uint32_t macrotile_nv12_factor = 0;
  uint32_t macrotile_factor = 0;
  uint32_t linear_factor = 0;
  uint32_t scale_factor = 0;
  uint32_t extra_fudge_factor = 0;
  uint32_t amortizable_threshold = 0;
  uint32_t system_overhead_lines = 0;
  bool has_bwc = false;
  bool has_ubwc = false;
  bool has_decimation = false;
  bool has_macrotile = false;
  bool has_non_scalar_rgb = false;
  bool is_src_split = false;
  bool perf_calc = false;
  bool has_dyn_bw_support = false;
  bool separate_rotator = false;
  bool has_qseed3 = false;
  bool has_concurrent_writeback = false;
  bool has_ppp = false;
  uint32_t writeback_index = kHWBlockMax;
  HWDynBwLimitInfo dyn_bw_info;
  std::vector<HWPipeCaps> hw_pipes;
  FormatsMap supported_formats_map;
  HWRotatorInfo hw_rot_info;
  HWDestScalarInfo hw_dest_scalar_info;
  bool has_avr = false;
  bool has_hdr = false;
  SmartDMARevision smart_dma_rev = SmartDMARevision::V1;
};

struct HWSplitInfo {
  uint32_t left_split = 0;
  uint32_t right_split = 0;

  bool operator !=(const HWSplitInfo &split_info) {
    return ((left_split != split_info.left_split) || (right_split != split_info.right_split));
  }

  bool operator ==(const HWSplitInfo &split_info) {
    return !(operator !=(split_info));
  }
};

enum HWS3DMode {
  kS3DModeNone,
  kS3DModeLR,
  kS3DModeRL,
  kS3DModeTB,
  kS3DModeFP,
  kS3DModeMax,
};

struct HWColorPrimaries {
  uint32_t white_point[2] = {};       // White point
  uint32_t red[2] = {};               // Red color primary
  uint32_t green[2] = {};             // Green color primary
  uint32_t blue[2] = {};              // Blue color primary
};

struct HWPanelOrientation {
  bool rotation = false;
  bool flip_horizontal = false;
  bool flip_vertical = false;
};

struct HWPanelInfo {
  DisplayPort port = kPortDefault;    // Display port
  HWDisplayMode mode = kModeDefault;  // Display mode
  bool partial_update = false;        // Partial update feature
  int left_align = 1;                 // ROI left alignment restriction
  int width_align = 1;                // ROI width alignment restriction
  int top_align = 1;                  // ROI top alignment restriction
  int height_align = 1;               // ROI height alignment restriction
  int min_roi_width = 1;              // Min width needed for ROI
  int min_roi_height = 1;             // Min height needed for ROI
  bool needs_roi_merge = false;       // Merge ROI's of both the DSI's
  bool dynamic_fps = false;           // Panel Supports dynamic fps
  bool dfps_porch_mode = false;       // dynamic fps VFP or HFP mode
  bool ping_pong_split = false;       // Supports Ping pong split
  uint32_t min_fps = 0;               // Min fps supported by panel
  uint32_t max_fps = 0;               // Max fps supported by panel
  bool is_primary_panel = false;      // Panel is primary display
  bool is_pluggable = false;          // Panel is pluggable
  HWSplitInfo split_info;             // Panel split configuration
  char panel_name[256] = {0};         // Panel name
  HWS3DMode s3d_mode = kS3DModeNone;  // Panel's current s3d mode.
  int panel_max_brightness = 0;       // Max panel brightness
  uint32_t left_roi_count = 1;        // Number if ROI supported on left panel
  uint32_t right_roi_count = 1;       // Number if ROI supported on right panel
  bool hdr_enabled = false;           // HDR feature supported
  uint32_t peak_luminance = 0;        // Panel's peak luminance level
  uint32_t average_luminance = 0;     // Panel's average luminance level
  uint32_t blackness_level = 0;       // Panel's blackness level
  HWColorPrimaries primaries = {};    // WRGB color primaries
  HWPanelOrientation panel_orientation = {};  // Panel Orientation
  bool bitclk_update = false;         // Bit clk can be updated to avoid RF interference.
  std::vector<uint64_t> bitclk_rates; // Supported bit clk levels.

  bool operator !=(const HWPanelInfo &panel_info) {
    return ((port != panel_info.port) || (mode != panel_info.mode) ||
            (partial_update != panel_info.partial_update) ||
            (left_align != panel_info.left_align) || (width_align != panel_info.width_align) ||
            (top_align != panel_info.top_align) || (height_align != panel_info.height_align) ||
            (min_roi_width != panel_info.min_roi_width) ||
            (min_roi_height != panel_info.min_roi_height) ||
            (needs_roi_merge != panel_info.needs_roi_merge) ||
            (dynamic_fps != panel_info.dynamic_fps) || (min_fps != panel_info.min_fps) ||
            (dfps_porch_mode != panel_info.dfps_porch_mode) ||
            (ping_pong_split != panel_info.ping_pong_split) ||
            (max_fps != panel_info.max_fps) || (is_primary_panel != panel_info.is_primary_panel) ||
            (split_info != panel_info.split_info) || (s3d_mode != panel_info.s3d_mode) ||
            (left_roi_count != panel_info.left_roi_count) ||
            (right_roi_count != panel_info.right_roi_count) ||
            (bitclk_update != panel_info.bitclk_update) ||
            (bitclk_rates != panel_info.bitclk_rates));
  }

  bool operator ==(const HWPanelInfo &panel_info) {
    return !(operator !=(panel_info));
  }
};

struct HWSessionConfig {
  LayerRect src_rect {};
  LayerRect dst_rect {};
  uint32_t buffer_count = 0;
  bool secure = false;
  uint32_t frame_rate = 0;
  LayerTransform transform;
  bool secure_camera = false;

  bool operator==(const HWSessionConfig& config) const {
    return (src_rect == config.src_rect &&
            dst_rect == config.dst_rect &&
            buffer_count == config.buffer_count &&
            secure == config.secure &&
            frame_rate == config.frame_rate &&
            transform == config.transform &&
            secure_camera == config.secure_camera);
  }

  bool operator!=(const HWSessionConfig& config) const {
    return !operator==(config);
  }
};

struct HWRotateInfo {
  int pipe_id = -1;  // Not actual pipe id, but the relative DMA id
  int writeback_id = -1;  // Writeback block id, but this is the same as DMA id
  LayerRect src_roi {};  // Source crop of each split
  LayerRect dst_roi {};  // Destination crop of each split
  bool valid = false;
  int rotate_id = -1;  // Actual rotator session id with driver
};

struct HWRotatorSession {
  HWRotateInfo hw_rotate_info[kMaxRotatePerLayer] {};
  uint32_t hw_block_count = 0;  // number of rotator hw blocks used by rotator session
  int session_id = -1;  // A handle with Session Manager
  HWSessionConfig hw_session_config {};
  LayerBuffer input_buffer {};  // Input to rotator
  LayerBuffer output_buffer {};  // Output of rotator, crop width and stride are same
  float input_compression = 1.0f;
  float output_compression = 1.0f;
  bool is_buffer_cached = false;
};

struct HWScaleLutInfo {
  uint32_t dir_lut_size = 0;
  uint32_t cir_lut_size = 0;
  uint32_t sep_lut_size = 0;
  uint64_t dir_lut = 0;
  uint64_t cir_lut = 0;
  uint64_t sep_lut = 0;
};

struct HWDetailEnhanceData : DisplayDetailEnhancerData {
  uint16_t prec_shift = 0;
  int16_t adjust_a[MAX_DETAIL_ENHANCE_CURVE] = {0};
  int16_t adjust_b[MAX_DETAIL_ENHANCE_CURVE] = {0};
  int16_t adjust_c[MAX_DETAIL_ENHANCE_CURVE] = {0};
};

struct HWPixelExtension {
  int32_t extension = 0;  // Number of pixels extension in left, right, top and bottom directions
                          // for all color components. This pixel value for each color component
                          // should be sum of fetch and repeat pixels.

  int32_t overfetch = 0;  // Number of pixels need to be overfetched in left, right, top and bottom
                          // directions from source image for scaling.

  int32_t repeat = 0;     // Number of pixels need to be repeated in left, right, top and bottom
                          // directions for scaling.
};

struct HWPlane {
  int32_t init_phase_x = 0;
  int32_t phase_step_x = 0;
  int32_t init_phase_y = 0;
  int32_t phase_step_y = 0;
  HWPixelExtension left {};
  HWPixelExtension top {};
  HWPixelExtension right {};
  HWPixelExtension bottom {};
  uint32_t roi_width = 0;
  int32_t preload_x = 0;
  int32_t preload_y = 0;
  uint32_t src_width = 0;
  uint32_t src_height = 0;
};

struct HWScaleData {
  struct enable {
    uint8_t scale = 0;
    uint8_t direction_detection = 0;
    uint8_t detail_enhance = 0;
  } enable;
  uint32_t dst_width = 0;
  uint32_t dst_height = 0;
  HWPlane plane[MAX_PLANES] {};
  // scale_v2_data fields
  ScalingFilterConfig y_rgb_filter_cfg = kFilterEdgeDirected;
  ScalingFilterConfig uv_filter_cfg = kFilterEdgeDirected;
  HWAlphaInterpolation alpha_filter_cfg = kInterpolationPixelRepeat;
  HWBlendingFilter blend_cfg = kBlendFilterCircular;

  struct lut_flags {
    uint8_t lut_swap = 0;
    uint8_t lut_dir_wr = 0;
    uint8_t lut_y_cir_wr = 0;
    uint8_t lut_uv_cir_wr = 0;
    uint8_t lut_y_sep_wr = 0;
    uint8_t lut_uv_sep_wr = 0;
  } lut_flag;

  uint32_t dir_lut_idx = 0;
  /* for Y(RGB) and UV planes*/
  uint32_t y_rgb_cir_lut_idx = 0;
  uint32_t uv_cir_lut_idx = 0;
  uint32_t y_rgb_sep_lut_idx = 0;
  uint32_t uv_sep_lut_idx = 0;
  HWDetailEnhanceData detail_enhance {};
};

struct HWDestScaleInfo {
  uint32_t mixer_width = 0;
  uint32_t mixer_height = 0;
  bool scale_update = false;
  HWScaleData scale_data = {};
  LayerRect panel_roi = {};
};

typedef std::map<uint32_t, HWDestScaleInfo *> DestScaleInfoMap;

struct HWAVRInfo {
  bool enable = false;                // Flag to Enable AVR feature
  HWAVRModes mode = kContinuousMode;  // Specifies the AVR mode
};

struct HWPipeInfo {
  uint8_t rect = 255;
  uint32_t pipe_id = 0;
  HWSubBlockType sub_block_type = kHWSubBlockMax;
  LayerRect src_roi {};
  LayerRect dst_roi {};
  uint8_t horizontal_decimation = 0;
  uint8_t vertical_decimation = 0;
  HWScaleData scale_data {};
  uint32_t z_order = 0;
  uint8_t flags = 0;
  bool valid = false;
};

struct HWLayerConfig {
  HWPipeInfo left_pipe {};           // pipe for left side of output
  HWPipeInfo right_pipe {};          // pipe for right side of output
  HWRotatorSession hw_rotator_session {};
  float compression = 1.0f;
};

struct HWHDRLayerInfo {
  enum HDROperation {
    kNoOp,   // No-op.
    kSet,    // Sets the HDR MetaData - Start of HDR
    kReset,  // resets the previously set HDR Metadata, End of HDR
  };

  int32_t layer_index = -1;
  HDROperation operation = kNoOp;
};

struct HWLayersInfo {
  LayerStack *stack = NULL;        // Input layer stack. Set by the caller.
  uint32_t app_layer_count = 0;    // Total number of app layers. Must not be 0.
  uint32_t gpu_target_index = 0;   // GPU target layer index. 0 if not present.

  std::vector<Layer> hw_layers = {};  // Layers which need to be programmed on the HW

  uint32_t index[kMaxSDELayers] = {};   // Indexes of the layers from the layer stack which need to
                                        // be programmed on hardware.
  uint32_t roi_index[kMaxSDELayers] = {0};  // Stores the ROI index where the layers are visible.

  int sync_handle = -1;         // Release fence id for current draw cycle.
  int set_idle_time_ms = -1;    // Set idle time to the new specified value.
                                //    -1 indicates no change in idle time since last set value.

  std::vector<LayerRect> left_frame_roi = {};   // Left ROI.
  std::vector<LayerRect> right_frame_roi = {};  // Right ROI.
  LayerRect partial_fb_roi = {};   // Damaged area in framebuffer.

  bool roi_split = false;          // Indicates separated left and right ROI

  bool use_hw_cursor = false;      // Indicates that HWCursor pipe needs to be used for cursor layer
  DestScaleInfoMap dest_scale_info_map = {};
  HWHDRLayerInfo hdr_layer_info = {};
  Handle pvt_data = NULL;   // Private data used by sdm extension only.
};

struct HWLayers {
  HWLayersInfo info {};
  HWLayerConfig config[kMaxSDELayers] {};
  float output_compression = 1.0f;
  uint32_t bandwidth = 0;
  uint32_t clock = 0;
  HWAVRInfo hw_avr_info = {};
};

struct HWDisplayAttributes : DisplayConfigVariableInfo {
  bool is_device_split = false;
  uint32_t v_front_porch = 0;  //!< Vertical front porch of panel
  uint32_t v_back_porch = 0;   //!< Vertical back porch of panel
  uint32_t v_pulse_width = 0;  //!< Vertical pulse width of panel
  uint32_t h_total = 0;        //!< Total width of panel (hActive + hFP + hBP + hPulseWidth)
  uint32_t v_total = 0;        //!< Total height of panel (vActive + vFP + vBP + vPulseWidth)
  uint32_t clock_khz = 0;      //!< Stores the pixel clock of panel in khz
  std::bitset<32> s3d_config {}; //!< Stores the bit mask of S3D modes

  bool operator !=(const HWDisplayAttributes &display_attributes) {
    return ((is_device_split != display_attributes.is_device_split) ||
            (x_pixels != display_attributes.x_pixels) ||
            (y_pixels != display_attributes.y_pixels) ||
            (x_dpi != display_attributes.x_dpi) ||
            (y_dpi != display_attributes.y_dpi) ||
            (fps != display_attributes.fps) ||
            (vsync_period_ns != display_attributes.vsync_period_ns) ||
            (v_front_porch != display_attributes.v_front_porch) ||
            (v_back_porch != display_attributes.v_back_porch) ||
            (v_pulse_width != display_attributes.v_pulse_width) ||
            (h_total != display_attributes.h_total) ||
            (pixel_formats != display_attributes.pixel_formats) ||
            (clock_khz != display_attributes.clock_khz) ||
            (is_yuv != display_attributes.is_yuv));
  }

  bool operator ==(const HWDisplayAttributes &display_attributes) {
    return !(operator !=(display_attributes));
  }
};

struct HWMixerAttributes {
  uint32_t width = 0;                                  // Layer mixer width
  uint32_t height = 0;                                 // Layer mixer height
  uint32_t split_left = 0;
  LayerBufferFormat output_format = kFormatRGB101010;  // Layer mixer output format

  bool operator !=(const HWMixerAttributes &mixer_attributes) {
    return ((width != mixer_attributes.width) ||
            (height != mixer_attributes.height) ||
            (output_format != mixer_attributes.output_format) ||
            (split_left != mixer_attributes.split_left));
  }

  bool operator ==(const HWMixerAttributes &mixer_attributes) {
    return !(operator !=(mixer_attributes));
  }

  bool IsValid() {
    return (width > 0 && height > 0);
  }
};

}  // namespace sdm

#endif  // __HW_INFO_TYPES_H__

