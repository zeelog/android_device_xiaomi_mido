/*
* Copyright (c) 2017, The Linux Foundation. All rights reserved.
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

#include <cutils/properties.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <utils/constants.h>
#include <utils/debug.h>
#include <utils/formats.h>
#include <algorithm>
#include <array>
#include <sstream>
#include <string>
#include <fstream>

#include "hwc_display_external_test.h"
#include "hwc_debugger.h"

#define __CLASS__ "HWCDisplayExternalTest"

namespace sdm {

using std::array;

int HWCDisplayExternalTest::Create(CoreInterface *core_intf, hwc_procs_t const **hwc_procs,
                                   qService::QService *qservice, uint32_t panel_bpp,
                                   uint32_t pattern_type, HWCDisplay **hwc_display) {
  HWCDisplay *hwc_external_test = new HWCDisplayExternalTest(core_intf, hwc_procs, qservice,
                                                             panel_bpp, pattern_type);

  int status = static_cast<HWCDisplayExternalTest *>(hwc_external_test)->Init();
  if (status) {
    delete hwc_external_test;
    return status;
  }

  *hwc_display = hwc_external_test;

  DLOGI("panel_bpp %d, pattern_type %d", panel_bpp, pattern_type);

  return status;
}

void HWCDisplayExternalTest::Destroy(HWCDisplay *hwc_display) {
  static_cast<HWCDisplayExternalTest *>(hwc_display)->Deinit();

  delete hwc_display;
}

HWCDisplayExternalTest::HWCDisplayExternalTest(CoreInterface *core_intf,
                                               hwc_procs_t const **hwc_procs,
                                               qService::QService *qservice, uint32_t panel_bpp,
                                               uint32_t pattern_type)
  : HWCDisplay(core_intf, hwc_procs, kHDMI, HWC_DISPLAY_EXTERNAL, false, qservice,
               DISPLAY_CLASS_EXTERNAL), panel_bpp_(panel_bpp), pattern_type_(pattern_type) {
}

int HWCDisplayExternalTest::Init() {
  uint32_t external_width = 0;
  uint32_t external_height = 0;

  int status = HWCDisplay::Init();
  if (status) {
    return status;
  }

  buffer_allocator_ = new HWCBufferAllocator();

  status = CreateLayerStack();
  if (status) {
    Deinit();
    return status;
  }

  DisplayError error = HWCDisplay::GetMixerResolution(&external_width, &external_height);
  if (error != kErrorNone) {
    Deinit();
    return -EINVAL;
  }

  status = HWCDisplay::SetFrameBufferResolution(external_width, external_height);
  if (status) {
    Deinit();
    return status;
  }

  return status;
}

int HWCDisplayExternalTest::Deinit() {
  DestroyLayerStack();

  delete buffer_allocator_;
  buffer_allocator_ = NULL;

  return HWCDisplay::Deinit();
}


int HWCDisplayExternalTest::Prepare(hwc_display_contents_1_t *content_list) {
  int status = 0;

  if (secure_display_active_) {
    MarkLayersForGPUBypass(content_list);
    return status;
  }

  if (!content_list || !content_list->numHwLayers) {
    DLOGW("Invalid content list");
    return -EINVAL;
  }

  if (shutdown_pending_) {
    return 0;
  }

  DisplayError error = display_intf_->Prepare(&layer_stack_);
  if (error != kErrorNone) {
    if (error == kErrorShutDown) {
      shutdown_pending_ = true;
    } else if (error != kErrorPermission) {
      DLOGE("Prepare failed. Error = %d", error);
      // To prevent surfaceflinger infinite wait, flush the previous frame during Commit()
      // so that previous buffer and fences are released, and override the error.
      flush_ = true;
    }
  }

  MarkLayersForGPUBypass(content_list);

  return 0;
}

int HWCDisplayExternalTest::Commit(hwc_display_contents_1_t *content_list) {
  int status = 0;

  if (secure_display_active_) {
    return status;
  }

  if (!content_list || !content_list->numHwLayers) {
    DLOGW("Invalid content list");
    return -EINVAL;
  }

  if (shutdown_pending_) {
    return 0;
  }

  DumpInputBuffer();

  if (!flush_) {
    DisplayError error = kErrorUndefined;

    error = display_intf_->Commit(&layer_stack_);
    if (error == kErrorNone) {
      // A commit is successfully submitted, start flushing on failure now onwards.
      flush_on_error_ = true;
    } else {
      if (error == kErrorShutDown) {
        shutdown_pending_ = true;
        return status;
      } else if (error != kErrorPermission) {
        DLOGE("Commit failed. Error = %d", error);
        // To prevent surfaceflinger infinite wait, flush the previous frame during Commit()
        // so that previous buffer and fences are released, and override the error.
        flush_ = true;
      }
    }
  }

  return PostCommit(content_list);
}

void HWCDisplayExternalTest::SetSecureDisplay(bool secure_display_active, bool force_flush) {
  if (secure_display_active_ != secure_display_active) {
    secure_display_active_ = secure_display_active;

    if (secure_display_active_) {
      DisplayError error = display_intf_->Flush();
      if (error != kErrorNone) {
        DLOGE("Flush failed. Error = %d", error);
      }
    }
  }
  return;
}

int HWCDisplayExternalTest::Perform(uint32_t operation, ...) {
  return 0;
}

void HWCDisplayExternalTest::DumpInputBuffer() {
  if (!dump_frame_count_ || flush_ || !dump_input_layers_) {
    return;
  }

  const char *dir_path = "/data/misc/display/frame_dump_external";
  uint32_t width = buffer_info_.alloc_buffer_info.aligned_width;
  uint32_t height = buffer_info_.alloc_buffer_info.aligned_height;
  string format_str = GetFormatString(buffer_info_.buffer_config.format);

  char *buffer = reinterpret_cast<char *>(mmap(NULL, buffer_info_.alloc_buffer_info.size,
                                                PROT_READ|PROT_WRITE, MAP_SHARED,
                                                buffer_info_.alloc_buffer_info.fd, 0));
  if (buffer == MAP_FAILED) {
    DLOGW("mmap failed. err = %d", errno);
    return;
  }

  if (mkdir(dir_path, 0777) != 0 && errno != EEXIST) {
    DLOGW("Failed to create %s directory errno = %d, desc = %s", dir_path, errno, strerror(errno));
    return;
  }

  // if directory exists already, need to explicitly change the permission.
  if (errno == EEXIST && chmod(dir_path, 0777) != 0) {
    DLOGW("Failed to change permissions on %s directory", dir_path);
    return;
  }

  if (buffer) {
    std::stringstream dump_file_name;
    dump_file_name << dir_path;
    dump_file_name << "/input_layer_" << width << "x" << height << "_" << format_str << ".raw";

    std::fstream fs;
    fs.open(dump_file_name.str().c_str(), std::fstream::in | std::fstream::out | std::fstream::app);
    if (!fs.is_open()) {
      DLOGI("File open failed %s", dump_file_name.str().c_str());
      return;
    }

    fs.write(buffer, (std::streamsize)buffer_info_.alloc_buffer_info.size);
    fs.close();

    DLOGI("Frame Dump %s: is successful", dump_file_name.str().c_str());
  }

  // Dump only once as the content is going to be same for all draw cycles
  if (dump_frame_count_) {
    dump_frame_count_ = 0;
  }

  if (munmap(buffer, buffer_info_.alloc_buffer_info.size) != 0) {
    DLOGW("munmap failed. err = %d", errno);
    return;
  }
}

void HWCDisplayExternalTest::CalcCRC(uint32_t color_val, std::bitset<16> *crc_data) {
  std::bitset<16> color = {};
  std::bitset<16> temp_crc = {};

  switch (panel_bpp_) {
    case kDisplayBpp18:
      color = (color_val & 0xFC) << 8;
      break;
    case kDisplayBpp24:
      color = color_val << 8;
      break;
    case kDisplayBpp30:
      color = color_val << 6;
      break;
    default:
      return;
  }

  temp_crc[15] = (*crc_data)[0] ^ (*crc_data)[1] ^ (*crc_data)[2] ^ (*crc_data)[3] ^
                 (*crc_data)[4] ^ (*crc_data)[5] ^ (*crc_data)[6] ^ (*crc_data)[7] ^
                 (*crc_data)[8] ^ (*crc_data)[9] ^ (*crc_data)[10] ^ (*crc_data)[11] ^
                 (*crc_data)[12] ^ (*crc_data)[14] ^ (*crc_data)[15] ^ color[0] ^ color[1] ^
                 color[2] ^ color[3] ^ color[4] ^ color[5] ^ color[6] ^ color[7] ^ color[8] ^
                 color[9] ^ color[10] ^ color[11] ^ color[12] ^ color[14] ^ color[15];

  temp_crc[14] = (*crc_data)[12] ^ (*crc_data)[13] ^ color[12] ^ color[13];
  temp_crc[13] = (*crc_data)[11] ^ (*crc_data)[12] ^ color[11] ^ color[12];
  temp_crc[12] = (*crc_data)[10] ^ (*crc_data)[11] ^ color[10] ^ color[11];
  temp_crc[11] = (*crc_data)[9] ^ (*crc_data)[10] ^ color[9] ^ color[10];
  temp_crc[10] = (*crc_data)[8] ^ (*crc_data)[9] ^ color[8] ^ color[9];
  temp_crc[9] = (*crc_data)[7] ^ (*crc_data)[8] ^ color[7] ^ color[8];
  temp_crc[8] = (*crc_data)[6] ^ (*crc_data)[7] ^ color[6] ^ color[7];
  temp_crc[7] = (*crc_data)[5] ^ (*crc_data)[6] ^ color[5] ^ color[6];
  temp_crc[6] = (*crc_data)[4] ^ (*crc_data)[5] ^ color[4] ^ color[5];
  temp_crc[5] = (*crc_data)[3] ^ (*crc_data)[4] ^ color[3] ^ color[4];
  temp_crc[4] = (*crc_data)[2] ^ (*crc_data)[3] ^ color[2] ^ color[3];
  temp_crc[3] = (*crc_data)[1] ^ (*crc_data)[2] ^ (*crc_data)[15] ^ color[1] ^ color[2] ^ color[15];
  temp_crc[2] = (*crc_data)[0] ^ (*crc_data)[1] ^ (*crc_data)[14] ^ color[0] ^ color[1] ^ color[14];

  temp_crc[1] = (*crc_data)[1] ^ (*crc_data)[2] ^ (*crc_data)[3] ^ (*crc_data)[4] ^ (*crc_data)[5] ^
                (*crc_data)[6] ^ (*crc_data)[7] ^ (*crc_data)[8] ^ (*crc_data)[9] ^
                (*crc_data)[10] ^ (*crc_data)[11] ^ (*crc_data)[12] ^ (*crc_data)[13] ^
                (*crc_data)[14] ^ color[1] ^ color[2] ^ color[3] ^ color[4] ^ color[5] ^ color[6] ^
                color[7] ^ color[8] ^ color[9] ^ color[10] ^ color[11] ^ color[12] ^ color[13] ^
                color[14];

  temp_crc[0] = (*crc_data)[0] ^ (*crc_data)[1] ^ (*crc_data)[2] ^ (*crc_data)[3] ^ (*crc_data)[4] ^
                (*crc_data)[5] ^ (*crc_data)[6] ^ (*crc_data)[7] ^ (*crc_data)[8] ^ (*crc_data)[9] ^
                (*crc_data)[10] ^ (*crc_data)[11] ^ (*crc_data)[12] ^ (*crc_data)[13] ^
                (*crc_data)[15] ^ color[0] ^ color[1] ^ color[2] ^ color[3] ^ color[4] ^ color[5] ^
                color[6] ^ color[7] ^ color[8] ^ color[9] ^ color[10] ^ color[11] ^ color[12] ^
                color[13] ^ color[15];

  (*crc_data) = temp_crc;
}

int HWCDisplayExternalTest::FillBuffer() {
  uint8_t *buffer = reinterpret_cast<uint8_t *>(mmap(NULL, buffer_info_.alloc_buffer_info.size,
                                                PROT_READ|PROT_WRITE, MAP_SHARED,
                                                buffer_info_.alloc_buffer_info.fd, 0));
  if (buffer == MAP_FAILED) {
    DLOGE("mmap failed. err = %d", errno);
    return -EFAULT;
  }

  switch (pattern_type_) {
    case kPatternColorRamp:
      GenerateColorRamp(buffer);
      break;
    case kPatternBWVertical:
      GenerateBWVertical(buffer);
      break;
    case kPatternColorSquare:
      GenerateColorSquare(buffer);
      break;
    default:
      DLOGW("Invalid Pattern type %d", pattern_type_);
      return -EINVAL;
  }

  if (munmap(buffer, buffer_info_.alloc_buffer_info.size) != 0) {
    DLOGE("munmap failed. err = %d", errno);
    return -EFAULT;
  }

  return 0;
}

int HWCDisplayExternalTest::GetStride(LayerBufferFormat format, uint32_t width, uint32_t *stride) {
  switch (format) {
  case kFormatRGBA8888:
  case kFormatRGBA1010102:
    *stride = width * 4;
    break;
  case kFormatRGB888:
    *stride = width * 3;
    break;
  default:
    DLOGE("Unsupported format type %d", format);
    return -EINVAL;
  }

  return 0;
}

void HWCDisplayExternalTest::PixelCopy(uint32_t red, uint32_t green, uint32_t blue, uint32_t alpha,
                                       uint8_t **buffer) {
  LayerBufferFormat format = buffer_info_.buffer_config.format;

  switch (format) {
    case kFormatRGBA8888:
      *(*buffer)++ = UINT8(red & 0xFF);
      *(*buffer)++ = UINT8(green & 0xFF);
      *(*buffer)++ = UINT8(blue & 0xFF);
      *(*buffer)++ = UINT8(alpha & 0xFF);
      break;
    case kFormatRGB888:
      *(*buffer)++ = UINT8(red & 0xFF);
      *(*buffer)++ = UINT8(green & 0xFF);
      *(*buffer)++ = UINT8(blue & 0xFF);
      break;
    case kFormatRGBA1010102:
      // Lower 8 bits of red
      *(*buffer)++ = UINT8(red & 0xFF);

      // Upper 2 bits of Red + Lower 6 bits of green
      *(*buffer)++ = UINT8(((green & 0x3F) << 2) | ((red >> 0x8) & 0x3));

      // Upper 4 bits of green + Lower 4 bits of blue
      *(*buffer)++ = UINT8(((blue & 0xF) << 4) | ((green >> 6) & 0xF));

      // Upper 6 bits of blue + Lower 2 bits of alpha
      *(*buffer)++ = UINT8(((alpha & 0x3) << 6) | ((blue >> 4) & 0x3F));
      break;
    default:
      DLOGW("format not supported format = %d", format);
      break;
  }
}

void HWCDisplayExternalTest::GenerateColorRamp(uint8_t *buffer) {
  uint32_t width = buffer_info_.buffer_config.width;
  uint32_t height = buffer_info_.buffer_config.height;
  LayerBufferFormat format = buffer_info_.buffer_config.format;
  uint32_t aligned_width = buffer_info_.alloc_buffer_info.aligned_width;
  uint32_t buffer_stride = 0;

  uint32_t color_ramp = 0;
  uint32_t start_color_val = 0;
  uint32_t step_size = 1;
  uint32_t ramp_width = 0;
  uint32_t ramp_height = 0;
  uint32_t shift_by = 0;

  std::bitset<16> crc_red = {};
  std::bitset<16> crc_green = {};
  std::bitset<16> crc_blue = {};

  switch (panel_bpp_) {
    case kDisplayBpp18:
      ramp_height = 64;
      ramp_width = 64;
      shift_by = 2;
      break;
    case kDisplayBpp24:
      ramp_height = 64;
      ramp_width = 256;
      break;
    case kDisplayBpp30:
      ramp_height = 32;
      ramp_width = 256;
      start_color_val = 0x180;
      break;
    default:
      return;
  }

  GetStride(format, aligned_width, &buffer_stride);

  for (uint32_t loop_height = 0; loop_height < height; loop_height++) {
    uint32_t color_value = start_color_val;
    uint8_t *temp = buffer + (loop_height * buffer_stride);

    for (uint32_t loop_width = 0; loop_width < width; loop_width++) {
      if (color_ramp == kColorRedRamp) {
        PixelCopy(color_value, 0, 0, 0, &temp);
        CalcCRC(color_value, &crc_red);
        CalcCRC(0, &crc_green);
        CalcCRC(0, &crc_blue);
      }
      if (color_ramp == kColorGreenRamp) {
        PixelCopy(0, color_value, 0, 0, &temp);
        CalcCRC(0, &crc_red);
        CalcCRC(color_value, &crc_green);
        CalcCRC(0, &crc_blue);
      }
      if (color_ramp == kColorBlueRamp) {
        PixelCopy(0, 0, color_value, 0, &temp);
        CalcCRC(0, &crc_red);
        CalcCRC(0, &crc_green);
        CalcCRC(color_value, &crc_blue);
      }
      if (color_ramp == kColorWhiteRamp) {
        PixelCopy(color_value, color_value, color_value, 0, &temp);
        CalcCRC(color_value, &crc_red);
        CalcCRC(color_value, &crc_green);
        CalcCRC(color_value, &crc_blue);
      }

      color_value = (start_color_val + (((loop_width + 1) % ramp_width) * step_size)) << shift_by;
    }

    if (panel_bpp_ == kDisplayBpp30 && ((loop_height + 1) % ramp_height) == 0) {
      if (start_color_val == 0x180) {
        start_color_val = 0;
        step_size = 4;
      } else {
        start_color_val = 0x180;
        step_size = 1;
        color_ramp = (color_ramp + 1) % 4;
      }
      continue;
    }

    if (((loop_height + 1) % ramp_height) == 0) {
      color_ramp = (color_ramp + 1) % 4;
    }
  }

  DLOGI("CRC red %x", crc_red.to_ulong());
  DLOGI("CRC green %x", crc_green.to_ulong());
  DLOGI("CRC blue %x", crc_blue.to_ulong());
}

void HWCDisplayExternalTest::GenerateBWVertical(uint8_t *buffer) {
  uint32_t width = buffer_info_.buffer_config.width;
  uint32_t height = buffer_info_.buffer_config.height;
  LayerBufferFormat format = buffer_info_.buffer_config.format;
  uint32_t aligned_width = buffer_info_.alloc_buffer_info.aligned_width;
  uint32_t buffer_stride = 0;
  uint32_t bits_per_component = panel_bpp_ / 3;
  uint32_t max_color_val = (1 << bits_per_component) - 1;

  std::bitset<16> crc_red = {};
  std::bitset<16> crc_green = {};
  std::bitset<16> crc_blue = {};

  if (panel_bpp_ == kDisplayBpp18) {
    max_color_val <<= 2;
  }

  GetStride(format, aligned_width, &buffer_stride);

  for (uint32_t loop_height = 0; loop_height < height; loop_height++) {
    uint32_t color = 0;
    uint8_t *temp = buffer + (loop_height * buffer_stride);

    for (uint32_t loop_width = 0; loop_width < width; loop_width++) {
      if (color == kColorBlack) {
        PixelCopy(0, 0, 0, 0, &temp);
        CalcCRC(0, &crc_red);
        CalcCRC(0, &crc_green);
        CalcCRC(0, &crc_blue);
      }
      if (color == kColorWhite) {
        PixelCopy(max_color_val, max_color_val, max_color_val, 0, &temp);
        CalcCRC(max_color_val, &crc_red);
        CalcCRC(max_color_val, &crc_green);
        CalcCRC(max_color_val, &crc_blue);
      }

      color = (color + 1) % 2;
    }
  }

  DLOGI("CRC red %x", crc_red.to_ulong());
  DLOGI("CRC green %x", crc_green.to_ulong());
  DLOGI("CRC blue %x", crc_blue.to_ulong());
}

void HWCDisplayExternalTest::GenerateColorSquare(uint8_t *buffer) {
  uint32_t width = buffer_info_.buffer_config.width;
  uint32_t height = buffer_info_.buffer_config.height;
  LayerBufferFormat format = buffer_info_.buffer_config.format;
  uint32_t aligned_width = buffer_info_.alloc_buffer_info.aligned_width;
  uint32_t buffer_stride = 0;
  uint32_t max_color_val = 0;
  uint32_t min_color_val = 0;

  std::bitset<16> crc_red = {};
  std::bitset<16> crc_green = {};
  std::bitset<16> crc_blue = {};

  switch (panel_bpp_) {
    case kDisplayBpp18:
      max_color_val = 63 << 2;  // CEA Dynamic range for 18bpp 0 - 63
      min_color_val = 0;
      break;
    case kDisplayBpp24:
      max_color_val = 235;  // CEA Dynamic range for 24bpp 16 - 235
      min_color_val = 16;
      break;
    case kDisplayBpp30:
      max_color_val = 940;  // CEA Dynamic range for 30bpp 64 - 940
      min_color_val = 64;
      break;
    default:
      return;
  }

  array<array<uint32_t, 3>, 8> colors = {{
    {{max_color_val, max_color_val, max_color_val}},  // White Color
    {{max_color_val, max_color_val, min_color_val}},  // Yellow Color
    {{min_color_val, max_color_val, max_color_val}},  // Cyan Color
    {{min_color_val, max_color_val, min_color_val}},  // Green Color
    {{max_color_val, min_color_val, max_color_val}},  // Megenta Color
    {{max_color_val, min_color_val, min_color_val}},  // Red Color
    {{min_color_val, min_color_val, max_color_val}},  // Blue Color
    {{min_color_val, min_color_val, min_color_val}},  // Black Color
  }};

  GetStride(format, aligned_width, &buffer_stride);

  for (uint32_t loop_height = 0; loop_height < height; loop_height++) {
    uint32_t color = 0;
    uint8_t *temp = buffer + (loop_height * buffer_stride);

    for (uint32_t loop_width = 0; loop_width < width; loop_width++) {
      PixelCopy(colors[color][0], colors[color][1], colors[color][2], 0, &temp);
      CalcCRC(colors[color][0], &crc_red);
      CalcCRC(colors[color][1], &crc_green);
      CalcCRC(colors[color][2], &crc_blue);

      if (((loop_width + 1) % 64) == 0) {
        color = (color + 1) % colors.size();
      }
    }

    if (((loop_height + 1) % 64) == 0) {
      std::reverse(colors.begin(), (colors.end() - 1));
    }
  }

  DLOGI("CRC red %x", crc_red.to_ulong());
  DLOGI("CRC green %x", crc_green.to_ulong());
  DLOGI("CRC blue %x", crc_blue.to_ulong());
}

int HWCDisplayExternalTest::InitLayer(Layer *layer) {
  uint32_t active_config = 0;
  DisplayConfigVariableInfo var_info = {};

  GetActiveDisplayConfig(&active_config);

  GetDisplayAttributesForConfig(INT32(active_config), &var_info);

  layer->flags.updating = 1;
  layer->src_rect = LayerRect(0, 0, var_info.x_pixels, var_info.y_pixels);
  layer->dst_rect = layer->src_rect;
  layer->frame_rate = var_info.fps;
  layer->blending = kBlendingPremultiplied;

  layer->input_buffer.unaligned_width = var_info.x_pixels;
  layer->input_buffer.unaligned_height = var_info.y_pixels;
  buffer_info_.buffer_config.format = kFormatRGBA8888;

  if (layer->composition != kCompositionGPUTarget) {
    buffer_info_.buffer_config.width = var_info.x_pixels;
    buffer_info_.buffer_config.height = var_info.y_pixels;
    switch (panel_bpp_) {
      case kDisplayBpp18:
      case kDisplayBpp24:
        buffer_info_.buffer_config.format = kFormatRGB888;
        break;
      case kDisplayBpp30:
        buffer_info_.buffer_config.format = kFormatRGBA1010102;
        break;
      default:
        DLOGW("panel bpp not supported %d", panel_bpp_);
        return -EINVAL;
    }
    buffer_info_.buffer_config.buffer_count = 1;

    int ret = buffer_allocator_->AllocateBuffer(&buffer_info_);
    if (ret != 0) {
      DLOGE("Buffer allocation failed. ret: %d", ret);
      return -ENOMEM;
    }

    ret = FillBuffer();
    if (ret != 0) {
      buffer_allocator_->FreeBuffer(&buffer_info_);
      return ret;
    }

    layer->input_buffer.width = buffer_info_.alloc_buffer_info.aligned_width;
    layer->input_buffer.height = buffer_info_.alloc_buffer_info.aligned_height;
    layer->input_buffer.size = buffer_info_.alloc_buffer_info.size;
    layer->input_buffer.planes[0].fd = buffer_info_.alloc_buffer_info.fd;
    layer->input_buffer.planes[0].stride = buffer_info_.alloc_buffer_info.stride;
    layer->input_buffer.format = buffer_info_.buffer_config.format;

    DLOGI("Input buffer WxH %dx%d format %s size %d fd %d stride %d", layer->input_buffer.width,
          layer->input_buffer.height, GetFormatString(layer->input_buffer.format),
          layer->input_buffer.size, layer->input_buffer.planes[0].fd,
          layer->input_buffer.planes[0].stride);
  }

  return 0;
}

int HWCDisplayExternalTest::DeinitLayer(Layer *layer) {
  if (layer->composition != kCompositionGPUTarget) {
    int ret = buffer_allocator_->FreeBuffer(&buffer_info_);
    if (ret != 0) {
      DLOGE("Buffer deallocation failed. ret: %d", ret);
      return -ENOMEM;
    }
  }

  return 0;
}

int HWCDisplayExternalTest::CreateLayerStack() {
  for (uint32_t i = 0; i < (kTestLayerCnt + 1 /* one dummy gpu_target layer */); i++) {
    Layer *layer = new Layer();

    if (i == kTestLayerCnt) {
      layer->composition = kCompositionGPUTarget;
    }

    int ret = InitLayer(layer);
    if (ret != 0) {
      delete layer;
      return ret;
    }
    layer_stack_.layers.push_back(layer);
  }

  return 0;
}

int HWCDisplayExternalTest::DestroyLayerStack() {
  for (uint32_t i = 0; i < UINT32(layer_stack_.layers.size()); i++) {
    Layer *layer = layer_stack_.layers.at(i);
    int ret = DeinitLayer(layer);
    if (ret != 0) {
      return ret;
    }

    delete layer;
  }

  layer_stack_.layers = {};

  return 0;
}

int HWCDisplayExternalTest::PostCommit(hwc_display_contents_1_t *content_list) {
  int status = 0;

  // Do no call flush on errors, if a successful buffer is never submitted.
  if (flush_ && flush_on_error_) {
    display_intf_->Flush();
  }

  if (!flush_) {
    for (size_t i = 0; i < layer_stack_.layers.size(); i++) {
      Layer *layer = layer_stack_.layers.at(i);
      LayerBuffer &layer_buffer = layer->input_buffer;

      close(layer_buffer.release_fence_fd);
      layer_buffer.release_fence_fd = -1;
    }

    close(layer_stack_.retire_fence_fd);
    layer_stack_.retire_fence_fd = -1;
    content_list->retireFenceFd = -1;
  }

  flush_ = false;

  return status;
}

}  // namespace sdm

