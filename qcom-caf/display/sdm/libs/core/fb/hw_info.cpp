/*
* Copyright (c) 2015-2017, The Linux Foundation. All rights reserved.
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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <utils/constants.h>
#include <utils/debug.h>
#include <utils/sys.h>
#include <dlfcn.h>

#include <algorithm>
#include <iostream>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "hw_info.h"

#define __CLASS__ "HWInfo"

using std::vector;
using std::map;
using std::string;
using std::fstream;
using std::to_string;

namespace sdm {

// kDefaultFormatSupport contains the bit map of supported formats for each hw blocks.
// For eg: if Cursor supports MDP_RGBA_8888[bit-13] and MDP_RGB_565[bit-0], then cursor pipe array
// contains { 0x01[0-3], 0x00[4-7], 0x00[8-12], 0x01[13-16], 0x00[17-20], 0x00[21-24], 0x00[24-28] }
const std::bitset<8> HWInfo::kDefaultFormatSupport[kHWSubBlockMax][
                                                      BITS_TO_BYTES(MDP_IMGTYPE_LIMIT1)] = {
  { 0xFF, 0xF5, 0x1C, 0x1E, 0x20, 0xFF, 0x01, 0x00, 0xFE, 0x1F },  // kHWVIGPipe
  { 0x33, 0xE0, 0x00, 0x16, 0x00, 0xBF, 0x00, 0x00, 0xFE, 0x07 },  // kHWRGBPipe
  { 0x33, 0xE0, 0x00, 0x16, 0x00, 0xBF, 0x00, 0x00, 0xFE, 0x07 },  // kHWDMAPipe
  { 0x12, 0x60, 0x0C, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00 },  // kHWCursorPipe
  { 0xFF, 0xF5, 0x1C, 0x1E, 0x20, 0xFF, 0x01, 0x00, 0xFE, 0x1F },  // kHWRotatorInput
  { 0xFF, 0xF5, 0x1C, 0x1E, 0x20, 0xFF, 0x01, 0x00, 0xFE, 0x1F },  // kHWRotatorOutput
  { 0x3F, 0xF4, 0x10, 0x1E, 0x20, 0xFF, 0x01, 0x00, 0xAA, 0x16 },  // kHWWBIntfOutput
};

int HWInfo::ParseString(const char *input, char *tokens[], const uint32_t max_token,
                        const char *delim, uint32_t *count) {
  char *tmp_token = NULL;
  char *temp_ptr;
  uint32_t index = 0;
  if (!input) {
    return -1;
  }
  tmp_token = strtok_r(const_cast<char *>(input), delim, &temp_ptr);
  while (tmp_token && index < max_token) {
    tokens[index++] = tmp_token;
    tmp_token = strtok_r(NULL, delim, &temp_ptr);
  }
  *count = index;

  return 0;
}

DisplayError HWInfo::GetDynamicBWLimits(HWResourceInfo *hw_resource) {
  Sys::fstream fs(kBWModeBitmap, fstream::in);
  if (!fs.is_open()) {
    DLOGE("File '%s' not found", kBWModeBitmap);
    return kErrorHardware;
  }

  HWDynBwLimitInfo* bw_info = &hw_resource->dyn_bw_info;
  for (int index = 0; index < kBwModeMax; index++) {
    bw_info->total_bw_limit[index] = UINT32(hw_resource->max_bandwidth_low);
    bw_info->pipe_bw_limit[index] = hw_resource->max_pipe_bw;
  }

  uint32_t token_count = 0;
  const uint32_t max_count = kBwModeMax;
  char *tokens[max_count] = { NULL };
  string line;
  while (Sys::getline_(fs, line)) {
    if (!ParseString(line.c_str(), tokens, max_count, ":, =\n", &token_count)) {
      if (!strncmp(tokens[0], "default_pipe", strlen("default_pipe"))) {
        bw_info->pipe_bw_limit[kBwDefault] = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "camera_pipe", strlen("camera_pipe"))) {
        bw_info->pipe_bw_limit[kBwCamera] = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "vflip_pipe", strlen("vflip_pipe"))) {
        bw_info->pipe_bw_limit[kBwVFlip] = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "hflip_pipe", strlen("hflip_pipe"))) {
        bw_info->pipe_bw_limit[kBwHFlip] = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "default", strlen("default"))) {
        bw_info->total_bw_limit[kBwDefault] = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "camera", strlen("camera"))) {
        bw_info->total_bw_limit[kBwCamera] = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "vflip", strlen("vflip"))) {
        bw_info->total_bw_limit[kBwVFlip] = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "hflip", strlen("hflip"))) {
        bw_info->total_bw_limit[kBwHFlip] = UINT32(atoi(tokens[1]));
      }
    }
  }

  return kErrorNone;
}

DisplayError HWInfo::GetHWResourceInfo(HWResourceInfo *hw_resource) {
  if (hw_resource_) {
    *hw_resource = *hw_resource_;
    return kErrorNone;
  }
  string fb_path = "/sys/devices/virtual/graphics/fb"
                      + to_string(kHWCapabilitiesNode) + "/mdp/caps";

  Sys::fstream fs(fb_path, fstream::in);
  if (!fs.is_open()) {
    DLOGE("File '%s' not found", fb_path.c_str());
    return kErrorHardware;
  }

  hw_resource_ = new HWResourceInfo;

  InitSupportedFormatMap(hw_resource_);
  hw_resource_->hw_version = kHWMdssVersion5;

  uint32_t token_count = 0;
  const uint32_t max_count = 256;
  char *tokens[max_count] = { NULL };
  string line;
  while (Sys::getline_(fs, line)) {
    // parse the line and update information accordingly
    if (!ParseString(line.c_str(), tokens, max_count, ":, =\n", &token_count)) {
      if (!strncmp(tokens[0], "mdp_version", strlen("mdp_version"))) {
        hw_resource_->hw_version = UINT32(atoi(tokens[1]));  // HW Version 3/5
      } else if (!strncmp(tokens[0], "hw_rev", strlen("hw_rev"))) {
        hw_resource_->hw_revision = UINT32(atoi(tokens[1]));  // HW Rev, v1/v2
      } else if (!strncmp(tokens[0], "rot_input_fmts", strlen("rot_input_fmts"))) {
        ParseFormats(&tokens[1], (token_count - 1), kHWRotatorInput, hw_resource_);
      } else if (!strncmp(tokens[0], "rot_output_fmts", strlen("rot_output_fmts"))) {
        ParseFormats(&tokens[1], (token_count - 1), kHWRotatorOutput, hw_resource_);
      } else if (!strncmp(tokens[0], "wb_output_fmts", strlen("wb_output_fmts"))) {
        ParseFormats(&tokens[1], (token_count - 1), kHWWBIntfOutput, hw_resource_);
      } else if (!strncmp(tokens[0], "blending_stages", strlen("blending_stages"))) {
        hw_resource_->num_blending_stages = UINT8(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "max_downscale_ratio", strlen("max_downscale_ratio"))) {
        hw_resource_->max_scale_down = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "max_upscale_ratio", strlen("max_upscale_ratio"))) {
        hw_resource_->max_scale_up = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "rot_dwnscale_max", strlen("rot_dwnscale_max"))) {
        hw_resource_->rot_downscale_max = FLOAT(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "max_bandwidth_low", strlen("max_bandwidth_low"))) {
        hw_resource_->max_bandwidth_low = UINT64(atol(tokens[1]));
      } else if (!strncmp(tokens[0], "max_bandwidth_high", strlen("max_bandwidth_high"))) {
        hw_resource_->max_bandwidth_high = UINT64(atol(tokens[1]));
      } else if (!strncmp(tokens[0], "max_mixer_width", strlen("max_mixer_width"))) {
        hw_resource_->max_mixer_width = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "max_pipe_width", strlen("max_pipe_width"))) {
        hw_resource_->max_pipe_width = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "max_cursor_size", strlen("max_cursor_size"))) {
        hw_resource_->max_cursor_size = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "max_pipe_bw", strlen("max_pipe_bw"))) {
        hw_resource_->max_pipe_bw = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "max_mdp_clk", strlen("max_mdp_clk"))) {
        hw_resource_->max_sde_clk = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "clk_fudge_factor", strlen("clk_fudge_factor"))) {
        hw_resource_->clk_fudge_factor = FLOAT(atoi(tokens[1])) / FLOAT(atoi(tokens[2]));
      } else if (!strncmp(tokens[0], "fmt_mt_nv12_factor", strlen("fmt_mt_nv12_factor"))) {
        hw_resource_->macrotile_nv12_factor = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "fmt_mt_factor", strlen("fmt_mt_factor"))) {
        hw_resource_->macrotile_factor = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "fmt_linear_factor", strlen("fmt_linear_factor"))) {
        hw_resource_->linear_factor = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "scale_factor", strlen("scale_factor"))) {
        hw_resource_->scale_factor = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "xtra_ff_factor", strlen("xtra_ff_factor"))) {
        hw_resource_->extra_fudge_factor = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "amortizable_threshold", strlen("amortizable_threshold"))) {
        hw_resource_->amortizable_threshold = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "system_overhead_lines", strlen("system_overhead_lines"))) {
        hw_resource_->system_overhead_lines = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "wb_intf_index", strlen("wb_intf_index"))) {
        hw_resource_->writeback_index = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "dest_scaler_count", strlen("dest_scaler_count"))) {
        hw_resource_->hw_dest_scalar_info.count = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "max_dest_scale_up", strlen("max_dest_scale_up"))) {
        hw_resource_->hw_dest_scalar_info.max_scale_up = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "max_dest_scaler_input_width",
                 strlen("max_dest_scaler_input_width"))) {
        hw_resource_->hw_dest_scalar_info.max_input_width = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "max_dest_scaler_output_width",
                 strlen("max_dest_scaler_output_width"))) {
        hw_resource_->hw_dest_scalar_info.max_output_width = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "features", strlen("features"))) {
        for (uint32_t i = 0; i < token_count; i++) {
          if (!strncmp(tokens[i], "bwc", strlen("bwc"))) {
            hw_resource_->has_bwc = true;
          } else if (!strncmp(tokens[i], "ubwc", strlen("ubwc"))) {
            hw_resource_->has_ubwc = true;
          } else if (!strncmp(tokens[i], "decimation", strlen("decimation"))) {
            hw_resource_->has_decimation = true;
          } else if (!strncmp(tokens[i], "tile_format", strlen("tile_format"))) {
            hw_resource_->has_macrotile = true;
          } else if (!strncmp(tokens[i], "src_split", strlen("src_split"))) {
            hw_resource_->is_src_split = true;
          } else if (!strncmp(tokens[i], "non_scalar_rgb", strlen("non_scalar_rgb"))) {
            hw_resource_->has_non_scalar_rgb = true;
          } else if (!strncmp(tokens[i], "perf_calc", strlen("perf_calc"))) {
            hw_resource_->perf_calc = true;
          } else if (!strncmp(tokens[i], "dynamic_bw_limit", strlen("dynamic_bw_limit"))) {
            hw_resource_->has_dyn_bw_support = true;
          } else if (!strncmp(tokens[i], "separate_rotator", strlen("separate_rotator"))) {
            hw_resource_->separate_rotator = true;
          } else if (!strncmp(tokens[i], "qseed3", strlen("qseed3"))) {
            hw_resource_->has_qseed3 = true;
          } else if (!strncmp(tokens[i], "has_ppp", strlen("has_ppp"))) {
            hw_resource_->has_ppp = true;
          } else if (!strncmp(tokens[i], "concurrent_writeback", strlen("concurrent_writeback"))) {
            hw_resource_->has_concurrent_writeback = true;
          } else if (!strncmp(tokens[i], "avr", strlen("avr"))) {
            hw_resource_->has_avr = true;
          } else if (!strncmp(tokens[i], "hdr", strlen("hdr"))) {
            hw_resource_->has_hdr = true;
          }
        }
      } else if (!strncmp(tokens[0], "pipe_count", strlen("pipe_count"))) {
        uint32_t pipe_count = UINT8(atoi(tokens[1]));
        for (uint32_t i = 0; i < pipe_count; i++) {
          Sys::getline_(fs, line);
          if (!ParseString(line.c_str(), tokens, max_count, ": =\n", &token_count)) {
            HWPipeCaps pipe_caps;
            pipe_caps.type = kPipeTypeUnused;
            for (uint32_t j = 0; j < token_count; j += 2) {
              if (!strncmp(tokens[j], "pipe_type", strlen("pipe_type"))) {
                if (!strncmp(tokens[j+1], "vig", strlen("vig"))) {
                  pipe_caps.type = kPipeTypeVIG;
                  hw_resource_->num_vig_pipe++;
                } else if (!strncmp(tokens[j+1], "rgb", strlen("rgb"))) {
                  pipe_caps.type = kPipeTypeRGB;
                  hw_resource_->num_rgb_pipe++;
                } else if (!strncmp(tokens[j+1], "dma", strlen("dma"))) {
                  pipe_caps.type = kPipeTypeDMA;
                  hw_resource_->num_dma_pipe++;
                } else if (!strncmp(tokens[j+1], "cursor", strlen("cursor"))) {
                  pipe_caps.type = kPipeTypeCursor;
                  hw_resource_->num_cursor_pipe++;
                }
              } else if (!strncmp(tokens[j], "pipe_ndx", strlen("pipe_ndx"))) {
                pipe_caps.id = UINT32(atoi(tokens[j+1]));
              } else if (!strncmp(tokens[j], "rects", strlen("rects"))) {
                pipe_caps.max_rects = UINT32(atoi(tokens[j+1]));
              } else if (!strncmp(tokens[j], "fmts_supported", strlen("fmts_supported"))) {
                char *tokens_fmt[max_count] = { NULL };
                uint32_t token_fmt_count = 0;
                if (!ParseString(tokens[j+1], tokens_fmt, max_count, ",\n", &token_fmt_count)) {
                  if (pipe_caps.type == kPipeTypeVIG) {
                    ParseFormats(tokens_fmt, token_fmt_count, kHWVIGPipe, hw_resource_);
                  } else if (pipe_caps.type == kPipeTypeRGB) {
                    ParseFormats(tokens_fmt, token_fmt_count, kHWRGBPipe, hw_resource_);
                  } else if (pipe_caps.type == kPipeTypeDMA) {
                    ParseFormats(tokens_fmt, token_fmt_count, kHWDMAPipe, hw_resource_);
                  } else if (pipe_caps.type == kPipeTypeCursor) {
                    ParseFormats(tokens_fmt, token_fmt_count, kHWCursorPipe, hw_resource_);
                  }
                }
              }
            }
            hw_resource_->hw_pipes.push_back(pipe_caps);
          }
        }
      }
    }
  }

  // Disable destination scalar count to 0 if extension library is not present
  DynLib extension_lib;
  if (!extension_lib.Open("libsdmextension.so")) {
    hw_resource_->hw_dest_scalar_info.count = 0;
  }

  DLOGI("SDE Version = %d, SDE Revision = %x, RGB = %d, VIG = %d, DMA = %d, Cursor = %d",
        hw_resource_->hw_version, hw_resource_->hw_revision, hw_resource_->num_rgb_pipe,
        hw_resource_->num_vig_pipe, hw_resource_->num_dma_pipe, hw_resource_->num_cursor_pipe);
  DLOGI("Upscale Ratio = %d, Downscale Ratio = %d, Blending Stages = %d",
        hw_resource_->max_scale_up, hw_resource_->max_scale_down,
        hw_resource_->num_blending_stages);
  DLOGI("SourceSplit = %d QSEED3 = %d", hw_resource_->is_src_split, hw_resource_->has_qseed3);
  DLOGI("BWC = %d, UBWC = %d, Decimation = %d, Tile Format = %d Concurrent Writeback = %d",
        hw_resource_->has_bwc, hw_resource_->has_ubwc, hw_resource_->has_decimation,
        hw_resource_->has_macrotile, hw_resource_->has_concurrent_writeback);
  DLOGI("MaxLowBw = %" PRIu64 " , MaxHighBw = % " PRIu64 "", hw_resource_->max_bandwidth_low,
        hw_resource_->max_bandwidth_high);
  DLOGI("MaxPipeBw = %" PRIu64 " KBps, MaxSDEClock = % " PRIu64 " Hz, ClockFudgeFactor = %f",
        hw_resource_->max_pipe_bw, hw_resource_->max_sde_clk, hw_resource_->clk_fudge_factor);
  DLOGI("Prefill factors: Tiled_NV12 = %d, Tiled = %d, Linear = %d, Scale = %d, Fudge_factor = %d",
        hw_resource_->macrotile_nv12_factor, hw_resource_->macrotile_factor,
        hw_resource_->linear_factor, hw_resource_->scale_factor, hw_resource_->extra_fudge_factor);

  // Avoid rotator for MDP3 harware.
  if ((hw_resource_->separate_rotator || hw_resource_->num_dma_pipe) && !hw_resource_->has_ppp) {
    GetHWRotatorInfo(hw_resource_);
  }

  // If the driver doesn't spell out the wb index, assume it to be the number of rotators,
  // based on legacy implementation.
  if (hw_resource_->writeback_index == kHWBlockMax) {
    hw_resource_->writeback_index = hw_resource_->hw_rot_info.num_rotator;
  }

  if (hw_resource_->has_dyn_bw_support) {
    DisplayError ret = GetDynamicBWLimits(hw_resource_);
    if (ret != kErrorNone) {
      DLOGE("Failed to read dynamic band width info");
      return ret;
    }

    DLOGI("Has Support for multiple bw limits shown below");
    for (int index = 0; index < kBwModeMax; index++) {
      DLOGI("Mode-index=%d  total_bw_limit=%d and pipe_bw_limit=%d",
            index, hw_resource_->dyn_bw_info.total_bw_limit[index],
            hw_resource_->dyn_bw_info.pipe_bw_limit[index]);
    }
  }

  *hw_resource = *hw_resource_;

  return kErrorNone;
}

DisplayError HWInfo::GetHWRotatorInfo(HWResourceInfo *hw_resource) {
  if (GetMDSSRotatorInfo(hw_resource) != kErrorNone)
    return GetV4L2RotatorInfo(hw_resource);

  return kErrorNone;
}

DisplayError HWInfo::GetMDSSRotatorInfo(HWResourceInfo *hw_resource) {
  Sys::fstream fs(kRotatorCapsPath, fstream::in);
  if (!fs.is_open()) {
    DLOGW("File '%s' not found", kRotatorCapsPath);
    return kErrorNotSupported;
  }

  uint32_t token_count = 0;
  const uint32_t max_count = 10;
  char *tokens[max_count] = { NULL };
  string line;

  hw_resource->hw_rot_info.type = HWRotatorInfo::ROT_TYPE_MDSS;
  while (Sys::getline_(fs, line)) {
    if (!ParseString(line.c_str(), tokens, max_count, ":, =\n", &token_count)) {
      if (!strncmp(tokens[0], "wb_count", strlen("wb_count"))) {
        hw_resource->hw_rot_info.num_rotator = UINT8(atoi(tokens[1]));
        hw_resource->hw_rot_info.device_path = "/dev/mdss_rotator";
      } else if (!strncmp(tokens[0], "downscale", strlen("downscale"))) {
        hw_resource->hw_rot_info.has_downscale = UINT8(atoi(tokens[1]));
      }
    }
  }

  DLOGI("MDSS Rotator: Count = %d, Downscale = %d, Min_downscale = %f",
        hw_resource->hw_rot_info.num_rotator, hw_resource->hw_rot_info.has_downscale,
        hw_resource->hw_rot_info.min_downscale);

  return kErrorNone;
}

DisplayError HWInfo::GetV4L2RotatorInfo(HWResourceInfo *hw_resource) {
  string v4l2_path = "/sys/class/video4linux/video";
  const uint32_t kMaxV4L2Nodes = 64;
  bool found = false;

  for (uint32_t i = 0; (i < kMaxV4L2Nodes) && (false == found); i++) {
    string path = v4l2_path + to_string(i) + "/name";
    Sys::fstream fs(path, fstream::in);
    if (!fs.is_open()) {
      continue;
    }

    string line;
    if (Sys::getline_(fs, line) &&
        (!strncmp(line.c_str(), "sde_rotator", strlen("sde_rotator")))) {
       hw_resource->hw_rot_info.device_path = string("/dev/video" + to_string(i));
       hw_resource->hw_rot_info.num_rotator++;
       hw_resource->hw_rot_info.type = HWRotatorInfo::ROT_TYPE_V4L2;
       hw_resource->hw_rot_info.has_downscale = true;

       string caps_path = v4l2_path + to_string(i) + "/device/caps";
       Sys::fstream caps_fs(caps_path, fstream::in);

       if (caps_fs.is_open()) {
         uint32_t token_count = 0;
         const uint32_t max_count = 10;
         char *tokens[max_count] = { NULL };
         string caps;
         while (Sys::getline_(caps_fs, caps)) {
           if (!ParseString(caps.c_str(), tokens, max_count, ":, =\n", &token_count)) {
             if (!strncmp(tokens[0], "downscale_compression", strlen("downscale_compression"))) {
               hw_resource->hw_rot_info.downscale_compression = UINT8(atoi(tokens[1]));
             } else if (!strncmp(tokens[0], "min_downscale", strlen("min_downscale"))) {
               hw_resource->hw_rot_info.min_downscale = FLOAT(atof(tokens[1]));
             }
           }
         }
       }

       // We support only 1 rotator
       found = true;
    }
  }

  DLOGI("V4L2 Rotator: Count = %d, Downscale = %d, Min_downscale = %f, Downscale_compression = %d",
        hw_resource->hw_rot_info.num_rotator, hw_resource->hw_rot_info.has_downscale,
        hw_resource->hw_rot_info.min_downscale, hw_resource->hw_rot_info.downscale_compression);

  return kErrorNone;
}

LayerBufferFormat HWInfo::GetSDMFormat(int mdp_format) {
  switch (mdp_format) {
  case MDP_ARGB_8888:              return kFormatARGB8888;
  case MDP_RGBA_8888:              return kFormatRGBA8888;
  case MDP_BGRA_8888:              return kFormatBGRA8888;
  case MDP_XRGB_8888:              return kFormatXRGB8888;
  case MDP_RGBX_8888:              return kFormatRGBX8888;
  case MDP_BGRX_8888:              return kFormatBGRX8888;
  case MDP_RGBA_5551:              return kFormatRGBA5551;
  case MDP_RGBA_4444:              return kFormatRGBA4444;
  case MDP_RGB_888:                return kFormatRGB888;
  case MDP_BGR_888:                return kFormatBGR888;
  case MDP_RGB_565:                return kFormatRGB565;
  case MDP_BGR_565:                return kFormatBGR565;
  case MDP_RGBA_8888_UBWC:         return kFormatRGBA8888Ubwc;
  case MDP_RGBX_8888_UBWC:         return kFormatRGBX8888Ubwc;
  case MDP_RGB_565_UBWC:           return kFormatBGR565Ubwc;
  case MDP_Y_CB_CR_H2V2:           return kFormatYCbCr420Planar;
  case MDP_Y_CR_CB_H2V2:           return kFormatYCrCb420Planar;
  case MDP_Y_CR_CB_GH2V2:          return kFormatYCrCb420PlanarStride16;
  case MDP_Y_CBCR_H2V2:            return kFormatYCbCr420SemiPlanar;
  case MDP_Y_CRCB_H2V2:            return kFormatYCrCb420SemiPlanar;
  case MDP_Y_CBCR_H2V2_VENUS:      return kFormatYCbCr420SemiPlanarVenus;
  case MDP_Y_CBCR_H1V2:            return kFormatYCbCr422H1V2SemiPlanar;
  case MDP_Y_CRCB_H1V2:            return kFormatYCrCb422H1V2SemiPlanar;
  case MDP_Y_CBCR_H2V1:            return kFormatYCbCr422H2V1SemiPlanar;
  case MDP_Y_CRCB_H2V1:            return kFormatYCrCb422H2V1SemiPlanar;
  case MDP_Y_CBCR_H2V2_UBWC:       return kFormatYCbCr420SPVenusUbwc;
  case MDP_Y_CRCB_H2V2_VENUS:      return kFormatYCrCb420SemiPlanarVenus;
  case MDP_YCBYCR_H2V1:            return kFormatYCbCr422H2V1Packed;
  case MDP_RGBA_1010102:           return kFormatRGBA1010102;
  case MDP_ARGB_2101010:           return kFormatARGB2101010;
  case MDP_RGBX_1010102:           return kFormatRGBX1010102;
  case MDP_XRGB_2101010:           return kFormatXRGB2101010;
  case MDP_BGRA_1010102:           return kFormatBGRA1010102;
  case MDP_ABGR_2101010:           return kFormatABGR2101010;
  case MDP_BGRX_1010102:           return kFormatBGRX1010102;
  case MDP_XBGR_2101010:           return kFormatXBGR2101010;
  case MDP_RGBA_1010102_UBWC:      return kFormatRGBA1010102Ubwc;
  case MDP_RGBX_1010102_UBWC:      return kFormatRGBX1010102Ubwc;
  case MDP_Y_CBCR_H2V2_P010:       return kFormatYCbCr420P010;
  case MDP_Y_CBCR_H2V2_TP10_UBWC:  return kFormatYCbCr420TP10Ubwc;
  default:                         return kFormatInvalid;
  }
}

void HWInfo::InitSupportedFormatMap(HWResourceInfo *hw_resource) {
  hw_resource->supported_formats_map.clear();

  for (int sub_blk_type = INT(kHWVIGPipe); sub_blk_type < INT(kHWSubBlockMax); sub_blk_type++) {
    PopulateSupportedFormatMap(kDefaultFormatSupport[sub_blk_type], MDP_IMGTYPE_LIMIT1,
                               (HWSubBlockType)sub_blk_type, hw_resource);
  }
}

void HWInfo::ParseFormats(char *tokens[], uint32_t token_count, HWSubBlockType sub_blk_type,
                          HWResourceInfo *hw_resource) {
  if (token_count > BITS_TO_BYTES(MDP_IMGTYPE_LIMIT1)) {
    return;
  }

  std::unique_ptr<std::bitset<8>[]> format_supported(new std::bitset<8>[token_count]);
  for (uint32_t i = 0; i < token_count; i++) {
    format_supported[i] = UINT8(atoi(tokens[i]));
  }

  PopulateSupportedFormatMap(format_supported.get(), (token_count << 3), sub_blk_type, hw_resource);
}

void HWInfo::PopulateSupportedFormatMap(const std::bitset<8> *format_supported,
                                        uint32_t format_count, HWSubBlockType sub_blk_type,
                                        HWResourceInfo *hw_resource) {
  vector <LayerBufferFormat> supported_sdm_formats;
  for (uint32_t mdp_format = 0; mdp_format < format_count; mdp_format++) {
    if (format_supported[mdp_format >> 3][mdp_format & 7]) {
      LayerBufferFormat sdm_format = GetSDMFormat(INT(mdp_format));
      if (sdm_format != kFormatInvalid) {
        supported_sdm_formats.push_back(sdm_format);
      }
    }
  }

  hw_resource->supported_formats_map.erase(sub_blk_type);
  hw_resource->supported_formats_map.insert(make_pair(sub_blk_type, supported_sdm_formats));
}

DisplayError HWInfo::GetFirstDisplayInterfaceType(HWDisplayInterfaceInfo *hw_disp_info) {
  Sys::fstream fs("/sys/devices/virtual/graphics/fb0/msm_fb_type", fstream::in);
  if (!fs.is_open()) {
    return kErrorHardware;
  }

  string line;
  if (!Sys::getline_(fs, line)) {
    return kErrorHardware;
  }

  if (!strncmp(line.c_str(), "dtv panel", strlen("dtv panel")) ||
      !strncmp(line.c_str(), "dp panel", strlen("dp panel"))) {
    hw_disp_info->type = kHDMI;
    DLOGI("First display is HDMI");
  } else {
    hw_disp_info->type = kPrimary;
    DLOGI("First display is internal display");
  }

  fs.close();
  fs.open("/sys/devices/virtual/graphics/fb0/connected", fstream::in);
  if (!fs.is_open()) {
    // If fb0 is for a DSI/connected panel, then connected node will not exist.
    hw_disp_info->is_connected = true;
  } else {
    if (!Sys::getline_(fs, line)) {
      return kErrorHardware;
    }

    hw_disp_info->is_connected =  (!strncmp(line.c_str(), "1", strlen("1")));
  }

  return kErrorNone;
}

}  // namespace sdm

