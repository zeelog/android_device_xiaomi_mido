/*
* Copyright (c) 2015 - 2018, 2020, The Linux Foundation. All rights reserved.
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

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <ctype.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <utils/debug.h>
#include <utils/sys.h>
#include <utils/formats.h>

#include <vector>
#include <map>
#include <utility>

#include "hw_hdmi.h"

#define __CLASS__ "HWHDMI"

#define  MIN_HDR_RESET_WAITTIME_SEC 2

namespace sdm {

#ifdef MDP_HDR_STREAM
static int32_t GetEOTF(const GammaTransfer &transfer) {
  int32_t mdp_transfer = -1;

  switch (transfer) {
  case Transfer_SMPTE_ST2084:
    mdp_transfer = MDP_HDR_EOTF_SMTPE_ST2084;
    break;
  case Transfer_HLG:
    mdp_transfer = MDP_HDR_EOTF_HLG;
    break;
  default:
    DLOGW("Unknown Transfer: %d", transfer);
  }

  return mdp_transfer;
}

static int32_t GetColoriMetry(const LayerBuffer & layer_buffer) {
  bool is_yuv = layer_buffer.flags.video;
  int32_t colorimetry = -1;

  if (is_yuv) {
    switch (layer_buffer.color_metadata.colorPrimaries) {
    case ColorPrimaries_BT601_6_525:
    case ColorPrimaries_BT601_6_625:
      colorimetry = MDP_COLORIMETRY_YCBCR_ITU_R_BT_601;
      break;
    case ColorPrimaries_BT709_5:
      colorimetry = MDP_COLORIMETRY_YCBCR_ITU_R_BT_709;
      break;
    case ColorPrimaries_BT2020:
      colorimetry = MDP_COLORIMETRY_YCBCR_ITU_R_BT_2020_YCBCR;
      break;
    default:
      DLOGW("Unknown color primary = %d for YUV", layer_buffer.color_metadata.colorPrimaries);
    }
  }

  return colorimetry;
}

static int32_t GetPixelEncoding(const LayerBuffer &layer_buffer) {
  bool is_yuv = layer_buffer.flags.video;
  int32_t mdp_pixel_encoding = -1;
  mdp_pixel_encoding = MDP_PIXEL_ENCODING_RGB;  // set RGB as default

  if (is_yuv) {
    switch (layer_buffer.format) {
    case kFormatYCbCr420SemiPlanarVenus:
    case kFormatYCbCr420SPVenusUbwc:
    case kFormatYCbCr420Planar:
    case kFormatYCrCb420Planar:
    case kFormatYCrCb420PlanarStride16:
    case kFormatYCbCr420SemiPlanar:
    case kFormatYCrCb420SemiPlanar:
    case kFormatYCbCr420P010:
    case kFormatYCbCr420TP10Ubwc:
      mdp_pixel_encoding = MDP_PIXEL_ENCODING_YCBCR_420;
      break;
    case kFormatYCbCr422H2V1Packed:
    case kFormatYCrCb422H2V1SemiPlanar:
    case kFormatYCrCb422H1V2SemiPlanar:
    case kFormatYCbCr422H2V1SemiPlanar:
    case kFormatYCbCr422H1V2SemiPlanar:
      mdp_pixel_encoding = MDP_PIXEL_ENCODING_YCBCR_422;
      break;
    default:  // other yuv formats
      DLOGW("New YUV format = %d, need to add support", layer_buffer.format);
      break;
    }
  }

  return mdp_pixel_encoding;
}
static int32_t GetBitsPerComponent(const LayerBuffer &layer_buffer) {
  bool is_yuv = layer_buffer.flags.video;
  bool is_10_bit = Is10BitFormat(layer_buffer.format);
  int32_t mdp_bpc = -1;

  if (is_yuv) {
    mdp_bpc = is_10_bit ? MDP_YUV_10_BPC : MDP_YUV_8_BPC;
  } else {
    mdp_bpc = is_10_bit ? MDP_RGB_10_BPC : MDP_RGB_8_BPC;
  }

  return mdp_bpc;
}

static uint32_t GetRange(const ColorRange &range) {
  return ((range == Range_Full) ? MDP_DYNAMIC_RANGE_VESA : MDP_DYNAMIC_RANGE_CEA);
}

static uint32_t GetContentType(const LayerBuffer &layer_buffer) {
  return (layer_buffer.flags.video ? MDP_CONTENT_TYPE_VIDEO : MDP_CONTENT_TYPE_GRAPHICS);
}
#endif

static bool MapHDMIDisplayTiming(const msm_hdmi_mode_timing_info *mode,
                                 fb_var_screeninfo *info, bool hdr_enabled,
                                 DisplayInterfaceFormat fmt) {
  if (!mode || !info) {
    return false;
  }

  info->reserved[0] = 0;
  info->reserved[1] = 0;
  info->reserved[2] = 0;
  info->reserved[3] = (info->reserved[3] & 0xFFFF) | (mode->video_format << 16);
  info->xoffset = 0;
  info->yoffset = 0;
  info->xres = mode->active_h;
  info->yres = mode->active_v;
  info->pixclock = (mode->pixel_freq) * 1000;
  info->vmode = mode->interlaced ? FB_VMODE_INTERLACED : FB_VMODE_NONINTERLACED;
  info->right_margin = mode->front_porch_h;
  info->hsync_len = mode->pulse_width_h;
  info->left_margin = mode->back_porch_h;
  info->lower_margin = mode->front_porch_v;
  info->vsync_len = mode->pulse_width_v;
  info->upper_margin = mode->back_porch_v;

  if (fmt == DisplayInterfaceFormat::kFormatNone) {
    info->grayscale = V4L2_PIX_FMT_RGB24;
    // If the mode supports YUV420 set grayscale to the FOURCC value for YUV420.
    std::bitset<32> pixel_formats = mode->pixel_formats;
    if (pixel_formats[1] && !pixel_formats[0]) {
      info->grayscale = V4L2_PIX_FMT_NV12;
    }
    if (pixel_formats[1] && pixel_formats[0] && hdr_enabled) {
      info->grayscale = V4L2_PIX_FMT_NV12;
    }
  } else if (fmt == DisplayInterfaceFormat::kFormatRGB) {
    info->grayscale = V4L2_PIX_FMT_RGB24;
  } else if (fmt == DisplayInterfaceFormat::kFormatYUV) {
    info->grayscale = V4L2_PIX_FMT_NV12;
  } else {
    DLOGE("Invalid format!");
    return false;
  }

  if (!mode->active_low_h) {
    info->sync |= (uint32_t)FB_SYNC_HOR_HIGH_ACT;
  } else {
    info->sync &= (uint32_t)~FB_SYNC_HOR_HIGH_ACT;
  }

  if (!mode->active_low_v) {
    info->sync |= (uint32_t)FB_SYNC_VERT_HIGH_ACT;
  } else {
    info->sync &= (uint32_t)~FB_SYNC_VERT_HIGH_ACT;
  }

  return true;
}

HWHDMI::HWHDMI(BufferSyncHandler *buffer_sync_handler,  HWInfoInterface *hw_info_intf)
  : HWDevice(buffer_sync_handler), hw_scan_info_(), active_config_index_(0) {
  HWDevice::device_type_ = kDeviceHDMI;
  HWDevice::device_name_ = "HDMI Display Device";
  HWDevice::hw_info_intf_ = hw_info_intf;
  (void)hdr_reset_start_;
  (void)hdr_reset_end_;
  (void)reset_hdr_flag_;
  (void)cdm_color_space_;
  pref_fmt_ = DisplayInterfaceFormat::kFormatNone;
}

DisplayError HWHDMI::Init() {
  DisplayError error = kErrorNone;

  SetSourceProductInformation("vendor_name", "ro.product.manufacturer");
  SetSourceProductInformation("product_description", "ro.product.name");

  error = HWDevice::Init();
  if (error != kErrorNone) {
    return error;
  }

  mdp_dest_scalar_data_.resize(hw_resource_.hw_dest_scalar_info.count);

  error = ReadEDIDInfo();
  if (error != kErrorNone) {
    Deinit();
    return error;
  }

  if (!IsResolutionFilePresent()) {
    Deinit();
    return kErrorHardware;
  }

  error = ReadTimingInfo();
  if (error != kErrorNone) {
    Deinit();
    return error;
  }

  ReadScanInfo();

  GetPanelS3DMode();

  s3d_mode_sdm_to_mdp_.insert(std::pair<HWS3DMode, msm_hdmi_s3d_mode>
                             (kS3DModeNone, HDMI_S3D_NONE));
  s3d_mode_sdm_to_mdp_.insert(std::pair<HWS3DMode, msm_hdmi_s3d_mode>
                             (kS3DModeLR, HDMI_S3D_SIDE_BY_SIDE));
  s3d_mode_sdm_to_mdp_.insert(std::pair<HWS3DMode, msm_hdmi_s3d_mode>
                             (kS3DModeRL, HDMI_S3D_SIDE_BY_SIDE));
  s3d_mode_sdm_to_mdp_.insert(std::pair<HWS3DMode, msm_hdmi_s3d_mode>
                             (kS3DModeTB, HDMI_S3D_TOP_AND_BOTTOM));
  s3d_mode_sdm_to_mdp_.insert(std::pair<HWS3DMode, msm_hdmi_s3d_mode>
                             (kS3DModeFP, HDMI_S3D_FRAME_PACKING));

  return error;
}

DisplayError HWHDMI::GetNumDisplayAttributes(uint32_t *count) {
  *count = UINT32(hdmi_modes_.size());
  if (*count <= 0) {
    return kErrorHardware;
  }

  return kErrorNone;
}

DisplayError HWHDMI::GetActiveConfig(uint32_t *active_config_index) {
  *active_config_index = active_config_index_;
  return kErrorNone;
}

DisplayError HWHDMI::SetActiveConfig(uint32_t active_config) {
  active_config_index_ = active_config;
  return kErrorNone;
}

DisplayError HWHDMI::ReadEDIDInfo() {
  ssize_t length = -1;
  char edid_str[kPageSize] = {'\0'};
  char edid_path[kMaxStringLength] = {'\0'};
  snprintf(edid_path, sizeof(edid_path), "%s%d/edid_modes", fb_path_, fb_node_index_);
  int edid_file = Sys::open_(edid_path, O_RDONLY);
  if (edid_file < 0) {
    DLOGE("EDID file open failed.");
    return kErrorHardware;
  }

  length = Sys::pread_(edid_file, edid_str, sizeof(edid_str)-1, 0);
  if (length <= 0) {
    DLOGE("%s: edid_modes file empty");
    return kErrorHardware;
  }
  Sys::close_(edid_file);

  DLOGI("EDID mode string: %s", edid_str);
  while (length > 1 && isspace(edid_str[length-1])) {
    --length;
  }
  edid_str[length] = '\0';

  if (length > 0) {
    // Get EDID modes from the EDID string
    char *ptr = edid_str;
    const uint32_t edid_count_max = 128;
    char *tokens[edid_count_max] = { NULL };
    uint32_t hdmi_mode_count = 0;

    ParseLine(ptr, tokens, edid_count_max, &hdmi_mode_count);

    supported_video_modes_.resize(hdmi_mode_count);

    hdmi_modes_.resize(hdmi_mode_count);
    for (uint32_t i = 0; i < hdmi_mode_count; i++) {
      hdmi_modes_[i] = UINT32(atoi(tokens[i]));
    }
  }

  return kErrorNone;
}

DisplayError HWHDMI::GetDisplayAttributes(uint32_t index,
                                          HWDisplayAttributes *display_attributes) {
  DTRACE_SCOPED();

  if (index >= hdmi_modes_.size()) {
    return kErrorNotSupported;
  }

  // Get the resolution info from the look up table
  msm_hdmi_mode_timing_info *timing_mode = &supported_video_modes_[0];
  for (uint32_t i = 0; i < hdmi_modes_.size(); i++) {
    msm_hdmi_mode_timing_info *cur = &supported_video_modes_[i];
    if (cur->video_format == hdmi_modes_[index]) {
      timing_mode = cur;
      break;
    }
  }
  display_attributes->x_pixels = timing_mode->active_h;
  display_attributes->y_pixels = timing_mode->active_v;
  DLOGV("index = %d. x = %d. y=%d",index,timing_mode->active_h,timing_mode->active_v);
  display_attributes->v_front_porch = timing_mode->front_porch_v;
  display_attributes->v_back_porch = timing_mode->back_porch_v;
  display_attributes->v_pulse_width = timing_mode->pulse_width_v;
  uint32_t h_blanking = timing_mode->front_porch_h + timing_mode->back_porch_h +
      timing_mode->pulse_width_h;
  display_attributes->h_total = timing_mode->active_h + h_blanking;
  display_attributes->x_dpi = 0;
  display_attributes->y_dpi = 0;
  display_attributes->fps = timing_mode->refresh_rate / 1000;
  display_attributes->vsync_period_ns = UINT32(1000000000L / display_attributes->fps);
  display_attributes->is_device_split = false;
  if (display_attributes->x_pixels > hw_resource_.max_mixer_width) {
    display_attributes->is_device_split = true;
    display_attributes->h_total += h_blanking;
  }

  GetDisplayS3DSupport(index, display_attributes);
  std::bitset<32> pixel_formats = timing_mode->pixel_formats;
  display_attributes->pixel_formats = timing_mode->pixel_formats;

  display_attributes->is_yuv = pixel_formats[1];
  display_attributes->clock_khz = timing_mode->pixel_freq;

  return kErrorNone;
}

DisplayError HWHDMI::SetDisplayAttributes(uint32_t index) {
  DTRACE_SCOPED();

  if (index > hdmi_modes_.size()) {
    return kErrorNotSupported;
  }

  // Variable screen info
  fb_var_screeninfo vscreeninfo = {};
  if (Sys::ioctl_(device_fd_, FBIOGET_VSCREENINFO, &vscreeninfo) < 0) {
    IOCTL_LOGE(FBIOGET_VSCREENINFO, device_type_);
    return kErrorHardware;
  }

  DLOGI("GetInfo<Mode=%d %dx%d (%d,%d,%d),(%d,%d,%d) %dMHz>", (vscreeninfo.reserved[3] >>16 & 0xFF),
        vscreeninfo.xres, vscreeninfo.yres, vscreeninfo.right_margin, vscreeninfo.hsync_len,
        vscreeninfo.left_margin, vscreeninfo.lower_margin, vscreeninfo.vsync_len,
        vscreeninfo.upper_margin, vscreeninfo.pixclock/1000000);

  msm_hdmi_mode_timing_info *timing_mode = &supported_video_modes_[0];
  for (uint32_t i = 0; i < hdmi_modes_.size(); i++) {
    msm_hdmi_mode_timing_info *cur = &supported_video_modes_[i];
    if (cur->video_format == hdmi_modes_[index]) {
      timing_mode = cur;
      break;
    }
  }

  if (MapHDMIDisplayTiming(timing_mode, &vscreeninfo,
                           hw_panel_info_.hdr_enabled, pref_fmt_) == false) {
    return kErrorParameters;
  }

  msmfb_metadata metadata = {};
  metadata.op = metadata_op_vic;
  metadata.data.video_info_code = timing_mode->video_format;
  if (Sys::ioctl_(device_fd_, MSMFB_METADATA_SET, &metadata) < 0) {
    IOCTL_LOGE(MSMFB_METADATA_SET, device_type_);
    return kErrorHardware;
  }

  DLOGI("SetInfo<Mode=%d %dx%d (%d,%d,%d),(%d,%d,%d) %dMHz>", (vscreeninfo.reserved[3]>>16 & 0xFF),
        vscreeninfo.xres, vscreeninfo.yres, vscreeninfo.right_margin, vscreeninfo.hsync_len,
        vscreeninfo.left_margin, vscreeninfo.lower_margin, vscreeninfo.vsync_len,
        vscreeninfo.upper_margin, vscreeninfo.pixclock/1000000);

  vscreeninfo.activate = FB_ACTIVATE_NOW | FB_ACTIVATE_ALL | FB_ACTIVATE_FORCE;
  if (Sys::ioctl_(device_fd_, FBIOPUT_VSCREENINFO, &vscreeninfo) < 0) {
    IOCTL_LOGE(FBIOPUT_VSCREENINFO, device_type_);
    return kErrorHardware;
  }

  active_config_index_ = index;

  frame_rate_ = timing_mode->refresh_rate;

  // Get the display attributes for current active config index
  GetDisplayAttributes(active_config_index_, &display_attributes_);
  UpdateMixerAttributes();

  supported_s3d_modes_.clear();
  supported_s3d_modes_.push_back(kS3DModeNone);
  for (uint32_t mode = kS3DModeNone + 1; mode < kS3DModeMax; mode ++) {
    if (display_attributes_.s3d_config[(HWS3DMode)mode]) {
      supported_s3d_modes_.push_back((HWS3DMode)mode);
    }
  }

  SetS3DMode(kS3DModeNone);

  return kErrorNone;
}

DisplayError HWHDMI::SetDisplayFormat(uint32_t index, DisplayInterfaceFormat fmt) {

  // Check for the usage of index

  if (index > hdmi_modes_.size()) {
    return kErrorNotSupported;
  }
  pref_fmt_ = fmt;

  return kErrorNone;
}

DisplayError HWHDMI::SetConfigAttributes(uint32_t index, uint32_t width, uint32_t height)
{
    if (index >= hdmi_modes_.size()) {
    return kErrorNotSupported;
  }

  // Get the resolution info from the look up table
  msm_hdmi_mode_timing_info *timing_mode = &supported_video_modes_[0];
  for (uint32_t i = 0; i < hdmi_modes_.size(); i++) {
    msm_hdmi_mode_timing_info *cur = &supported_video_modes_[i];
    if (cur->video_format == hdmi_modes_[index]) {
      timing_mode = cur;
      break;
    }
  }

  timing_mode->active_h = width;
  timing_mode->active_v = height;
  return kErrorNone;
}

DisplayError HWHDMI::GetConfigIndex(uint32_t width, uint32_t height, uint32_t *index) {
  // Get the resolution info from the look up table
  for (uint32_t i = 0; i < hdmi_modes_.size(); i++) {
    msm_hdmi_mode_timing_info *cur = &supported_video_modes_[i];
    if (cur->active_h == width && cur->active_v == height) {
      *index = i;
      return kErrorNone;
    }
  }
  return kErrorNotSupported;
}

DisplayError HWHDMI::GetConfigIndex(uint32_t mode, uint32_t *index) {
  // Check if the mode is valid and return corresponding index
  for (uint32_t i = 0; i < hdmi_modes_.size(); i++) {
    if (hdmi_modes_[i] == mode) {
      *index = i;
      DLOGI("Index = %d for config = %d", *index, mode);
      return kErrorNone;
    }
  }

  DLOGE("Config = %d not supported", mode);
  return kErrorNotSupported;
}

DisplayError HWHDMI::Validate(HWLayers *hw_layers) {
  HWDevice::ResetDisplayParams();
  return HWDevice::Validate(hw_layers);
}

DisplayError HWHDMI::Commit(HWLayers *hw_layers) {
  DisplayError error = UpdateHDRMetaData(hw_layers);
  if (error != kErrorNone) {
    return error;
  }
  if (cdm_color_space_commit_) {
#ifdef MDP_COMMIT_UPDATE_CDM_COLOR_SPACE
    mdp_layer_commit_v1 &mdp_commit = mdp_disp_commit_.commit_v1;
    mdp_commit.cdm_color_space = cdm_color_space_;
    mdp_commit.flags |= MDP_COMMIT_UPDATE_CDM_COLOR_SPACE;
#endif
  }

  error = HWDevice::Commit(hw_layers);
  if (cdm_color_space_commit_)
    cdm_color_space_commit_ = false;

  return error;
}

DisplayError HWHDMI::GetHWScanInfo(HWScanInfo *scan_info) {
  if (!scan_info) {
    return kErrorParameters;
  }
  *scan_info = hw_scan_info_;
  return kErrorNone;
}

DisplayError HWHDMI::GetVideoFormat(uint32_t config_index, uint32_t *video_format) {
  if (config_index > hdmi_modes_.size()) {
    return kErrorNotSupported;
  }

  *video_format = hdmi_modes_[config_index];

  return kErrorNone;
}

DisplayError HWHDMI::GetMaxCEAFormat(uint32_t *max_cea_format) {
  *max_cea_format = HDMI_VFRMT_END;

  return kErrorNone;
}

DisplayError HWHDMI::OnMinHdcpEncryptionLevelChange(uint32_t min_enc_level) {
  DisplayError error = kErrorNone;
  int fd = -1;
  char data[kMaxStringLength] = "/sys/devices/virtual/hdcp/msm_hdcp/min_level_change";


  fd = Sys::open_(data, O_WRONLY);
  if (fd < 0) {
    DLOGW("File '%s' could not be opened.", data);
    return kErrorHardware;
  }

  snprintf(data, sizeof(data), "%d", min_enc_level);

  ssize_t err = Sys::pwrite_(fd, data, strlen(data), 0);
  if (err <= 0) {
    DLOGE("Write failed, Error = %s", strerror(errno));
    error = kErrorHardware;
  }

  Sys::close_(fd);

  return error;
}

HWScanSupport HWHDMI::MapHWScanSupport(uint32_t value) {
  switch (value) {
  // TODO(user): Read the scan type from driver defined values instead of hardcoding
  case 0:
    return kScanNotSupported;
  case 1:
    return kScanAlwaysOverscanned;
  case 2:
    return kScanAlwaysUnderscanned;
  case 3:
    return kScanBoth;
  default:
    return kScanNotSupported;
    break;
  }
}

void HWHDMI::ReadScanInfo() {
  int scan_info_file = -1;
  ssize_t len = -1;
  char data[kPageSize] = {'\0'};

  snprintf(data, sizeof(data), "%s%d/scan_info", fb_path_, fb_node_index_);
  scan_info_file = Sys::open_(data, O_RDONLY);
  if (scan_info_file < 0) {
    DLOGW("File '%s' not found.", data);
    return;
  }

  memset(&data[0], 0, sizeof(data));
  len = Sys::pread_(scan_info_file, data, sizeof(data) - 1, 0);
  if (len <= 0) {
    Sys::close_(scan_info_file);
    DLOGW("File %s%d/scan_info is empty.", fb_path_, fb_node_index_);
    return;
  }
  data[len] = '\0';
  Sys::close_(scan_info_file);

  const uint32_t scan_info_max_count = 3;
  uint32_t scan_info_count = 0;
  char *tokens[scan_info_max_count] = { NULL };
  ParseLine(data, tokens, scan_info_max_count, &scan_info_count);
  if (scan_info_count != scan_info_max_count) {
    DLOGW("Failed to parse scan info string %s", data);
    return;
  }

  hw_scan_info_.pt_scan_support = MapHWScanSupport(UINT32(atoi(tokens[0])));
  hw_scan_info_.it_scan_support = MapHWScanSupport(UINT32(atoi(tokens[1])));
  hw_scan_info_.cea_scan_support = MapHWScanSupport(UINT32(atoi(tokens[2])));
  DLOGI("PT %d IT %d CEA %d", hw_scan_info_.pt_scan_support, hw_scan_info_.it_scan_support,
        hw_scan_info_.cea_scan_support);
}

int HWHDMI::OpenResolutionFile(int file_mode) {
  char file_path[kMaxStringLength];
  memset(file_path, 0, sizeof(file_path));
  snprintf(file_path , sizeof(file_path), "%s%d/res_info", fb_path_, fb_node_index_);

  int fd = Sys::open_(file_path, file_mode);

  if (fd < 0) {
    DLOGE("file '%s' not found : ret = %d err str: %s", file_path, fd, strerror(errno));
  }

  return fd;
}

// Method to request HDMI driver to write a new page of timing info into res_info node
void HWHDMI::RequestNewPage(uint32_t page_number) {
  char page_string[kPageSize];
  int fd = OpenResolutionFile(O_WRONLY);
  if (fd < 0) {
    return;
  }

  snprintf(page_string, sizeof(page_string), "%d", page_number);

  DLOGI_IF(kTagDriverConfig, "page=%s", page_string);

  ssize_t err = Sys::pwrite_(fd, page_string, sizeof(page_string), 0);
  if (err <= 0) {
    DLOGE("Write to res_info failed (%s)", strerror(errno));
  }

  Sys::close_(fd);
}

// Reads the contents of res_info node into a buffer if the file is not empty
bool HWHDMI::ReadResolutionFile(char *config_buffer) {
  ssize_t bytes_read = 0;
  int fd = OpenResolutionFile(O_RDONLY);
  if (fd >= 0) {
    bytes_read = Sys::pread_(fd, config_buffer, kPageSize, 0);
    Sys::close_(fd);
  }

  DLOGI_IF(kTagDriverConfig, "bytes_read = %d", bytes_read);

  return (bytes_read > 0);
}

// Populates the internal timing info structure with the timing info obtained
// from the HDMI driver
DisplayError HWHDMI::ReadTimingInfo() {
  uint32_t config_index = 0;
  uint32_t page_number = MSM_HDMI_INIT_RES_PAGE;
  uint32_t size = sizeof(msm_hdmi_mode_timing_info);

  while (true) {
    char config_buffer[kPageSize] = {0};
    msm_hdmi_mode_timing_info *info = reinterpret_cast<msm_hdmi_mode_timing_info *>(config_buffer);
    RequestNewPage(page_number);

    if (!ReadResolutionFile(config_buffer)) {
      break;
    }

    while (info->video_format && size < kPageSize && config_index < hdmi_modes_.size()) {
      supported_video_modes_[config_index] = *info;
      size += sizeof(msm_hdmi_mode_timing_info);

      DLOGI_IF(kTagDriverConfig, "Config=%d Mode %d: (%dx%d) @ %d, pixel formats %d",
               config_index,
               supported_video_modes_[config_index].video_format,
               supported_video_modes_[config_index].active_h,
               supported_video_modes_[config_index].active_v,
               supported_video_modes_[config_index].refresh_rate,
               supported_video_modes_[config_index].pixel_formats);

      info++;
      config_index++;
    }

    size = sizeof(msm_hdmi_mode_timing_info);
    // Request HDMI driver to populate res_info with more
    // timing information
    page_number++;
  }

  if (page_number == MSM_HDMI_INIT_RES_PAGE || config_index == 0) {
    DLOGE("No timing information found.");
    return kErrorHardware;
  }

  return kErrorNone;
}

bool HWHDMI::IsResolutionFilePresent() {
  bool is_file_present = false;
  int fd = OpenResolutionFile(O_RDONLY);
  if (fd >= 0) {
    is_file_present = true;
    Sys::close_(fd);
  }

  return is_file_present;
}

void HWHDMI::SetSourceProductInformation(const char *node, const char *name) {
  char property_value[kMaxStringLength];
  char sys_fs_path[kMaxStringLength];
  int hdmi_node_index = GetFBNodeIndex(kDeviceHDMI);
  if (hdmi_node_index < 0) {
    return;
  }

  ssize_t length = 0;
  bool prop_read_success = Debug::GetProperty(name, property_value);
  if (!prop_read_success) {
    return;
  }

  snprintf(sys_fs_path , sizeof(sys_fs_path), "%s%d/%s", fb_path_, hdmi_node_index, node);
  length = HWDevice::SysFsWrite(sys_fs_path, property_value,
                                static_cast<ssize_t>(strlen(property_value)));
  if (length <= 0) {
    DLOGW("Failed to write %s = %s", node, property_value);
  }
}

DisplayError HWHDMI::GetDisplayS3DSupport(uint32_t index,
                                          HWDisplayAttributes *attrib) {
  ssize_t length = -1;
  char edid_s3d_str[kPageSize] = {'\0'};
  char edid_s3d_path[kMaxStringLength] = {'\0'};
  snprintf(edid_s3d_path, sizeof(edid_s3d_path), "%s%d/edid_3d_modes", fb_path_, fb_node_index_);

  if (index > hdmi_modes_.size()) {
    return kErrorNotSupported;
  }

  attrib->s3d_config[kS3DModeNone] = 1;

  // Three level inception!
  // The string looks like 16=SSH,4=FP:TAB:SSH,5=FP:SSH,32=FP:TAB:SSH
  // Initialize all the pointers to NULL to avoid crash in function strtok_r()
  char *saveptr_l1 = NULL, *saveptr_l2 = NULL, *saveptr_l3 = NULL;
  char *l1 = NULL, *l2 = NULL, *l3 = NULL;

  int edid_s3d_node = Sys::open_(edid_s3d_path, O_RDONLY);
  if (edid_s3d_node < 0) {
    DLOGW("%s could not be opened : %s", edid_s3d_path, strerror(errno));
    return kErrorNotSupported;
  }

  length = Sys::pread_(edid_s3d_node, edid_s3d_str, sizeof(edid_s3d_str)-1, 0);
  if (length <= 0) {
    Sys::close_(edid_s3d_node);
    return kErrorNotSupported;
  }

  l1 = strtok_r(edid_s3d_str, ",", &saveptr_l1);
  while (l1 != NULL) {
    l2 = strtok_r(l1, "=", &saveptr_l2);
    if (l2 != NULL) {
      if (hdmi_modes_[index] == (uint32_t)atoi(l2)) {
          l3 = strtok_r(saveptr_l2, ":", &saveptr_l3);
          while (l3 != NULL) {
            if (strncmp("SSH", l3, strlen("SSH")) == 0) {
              attrib->s3d_config[kS3DModeLR] = 1;
              attrib->s3d_config[kS3DModeRL] = 1;
            } else if (strncmp("TAB", l3, strlen("TAB")) == 0) {
              attrib->s3d_config[kS3DModeTB] = 1;
            } else if (strncmp("FP", l3, strlen("FP")) == 0) {
              attrib->s3d_config[kS3DModeFP] = 1;
            }
            l3 = strtok_r(NULL, ":", &saveptr_l3);
          }
      }
    }
    l1 = strtok_r(NULL, ",", &saveptr_l1);
  }

  Sys::close_(edid_s3d_node);
  return kErrorNone;
}

bool HWHDMI::IsSupportedS3DMode(HWS3DMode s3d_mode) {
  for (uint32_t i = 0; i < supported_s3d_modes_.size(); i++) {
    if (supported_s3d_modes_[i] == s3d_mode) {
      return true;
    }
  }
  return false;
}

DisplayError HWHDMI::SetS3DMode(HWS3DMode s3d_mode) {
  if (!IsSupportedS3DMode(s3d_mode)) {
    DLOGW("S3D mode is not supported s3d_mode = %d", s3d_mode);
    return kErrorNotSupported;
  }

  std::map<HWS3DMode, msm_hdmi_s3d_mode>::iterator it = s3d_mode_sdm_to_mdp_.find(s3d_mode);
  if (it == s3d_mode_sdm_to_mdp_.end()) {
    return kErrorNotSupported;
  }
  msm_hdmi_s3d_mode s3d_mdp_mode = it->second;

  if (active_mdp_s3d_mode_ == s3d_mdp_mode) {
    // HDMI_S3D_SIDE_BY_SIDE is an mdp mapping for kS3DModeLR and kS3DModeRL s3d modes. So no need
    // to update the s3d_mode node. hw_panel_info needs to be updated to differentiate these two s3d
    // modes in strategy
    hw_panel_info_.s3d_mode = s3d_mode;
    return kErrorNone;
  }

  ssize_t length = -1;
  char s3d_mode_path[kMaxStringLength] = {'\0'};
  char s3d_mode_string[kMaxStringLength] = {'\0'};
  snprintf(s3d_mode_path, sizeof(s3d_mode_path), "%s%d/s3d_mode", fb_path_, fb_node_index_);

  int s3d_mode_node = Sys::open_(s3d_mode_path, O_RDWR);
  if (s3d_mode_node < 0) {
    DLOGW("%s could not be opened : %s", s3d_mode_path, strerror(errno));
    return kErrorNotSupported;
  }

  snprintf(s3d_mode_string, sizeof(s3d_mode_string), "%d", s3d_mdp_mode);
  length = Sys::pwrite_(s3d_mode_node, s3d_mode_string, sizeof(s3d_mode_string), 0);
  if (length <= 0) {
    DLOGW("Failed to write into s3d node: %s", strerror(errno));
    Sys::close_(s3d_mode_node);
    return kErrorNotSupported;
  }

  active_mdp_s3d_mode_ = s3d_mdp_mode;
  hw_panel_info_.s3d_mode = s3d_mode;
  Sys::close_(s3d_mode_node);

  DLOGI_IF(kTagDriverConfig, "Set s3d mode %d", hw_panel_info_.s3d_mode);
  return kErrorNone;
}

DisplayError HWHDMI::GetPanelS3DMode() {
  ssize_t length = -1;
  char s3d_mode_path[kMaxStringLength] = {'\0'};
  char s3d_mode_string[kMaxStringLength] = {'\0'};
  snprintf(s3d_mode_path, sizeof(s3d_mode_path), "%s%d/s3d_mode", fb_path_, fb_node_index_);
  int panel_s3d_mode = 0;

  int s3d_mode_node = Sys::open_(s3d_mode_path, O_RDWR);
  if (s3d_mode_node < 0) {
    DLOGE("%s could not be opened : %s", s3d_mode_path, strerror(errno));
    return kErrorNotSupported;
  }

  length = Sys::pread_(s3d_mode_node, s3d_mode_string, sizeof(s3d_mode_string), 0);
  if (length <= 0) {
    DLOGE("Failed read s3d node: %s", strerror(errno));
    Sys::close_(s3d_mode_node);
    return kErrorNotSupported;
  }

  panel_s3d_mode = atoi(s3d_mode_string);
  if (panel_s3d_mode < HDMI_S3D_NONE || panel_s3d_mode >= HDMI_S3D_MAX) {
    Sys::close_(s3d_mode_node);
    DLOGW("HDMI panel S3D mode is not supported panel_s3d_mode = %d", panel_s3d_mode);
    return kErrorUndefined;
  }

  active_mdp_s3d_mode_  = static_cast<msm_hdmi_s3d_mode>(panel_s3d_mode);
  Sys::close_(s3d_mode_node);

  DLOGI_IF(kTagDriverConfig, "Get HDMI panel s3d mode %d", active_mdp_s3d_mode_);
  return kErrorNone;
}

DisplayError HWHDMI::GetDynamicFrameRateMode(uint32_t refresh_rate, uint32_t *mode,
                                             DynamicFPSData *data, uint32_t *config_index) {
  msm_hdmi_mode_timing_info *cur = NULL;
  msm_hdmi_mode_timing_info *dst = NULL;
  uint32_t i = 0;
  int pre_refresh_rate_diff = 0;
  bool pre_unstd_mode = false;

  for (i = 0; i < hdmi_modes_.size(); i++) {
    msm_hdmi_mode_timing_info *timing_mode = &supported_video_modes_[i];
    if (timing_mode->video_format == hdmi_modes_[active_config_index_]) {
      cur = timing_mode;
      break;
    }
  }

  if (cur == NULL) {
    DLOGE("can't find timing info for active config index(%d)", active_config_index_);
    return kErrorUndefined;
  }

  if (cur->refresh_rate != frame_rate_) {
    pre_unstd_mode = true;
  }

  if (i >= hdmi_modes_.size()) {
    return kErrorNotSupported;
  }

  dst = cur;
  pre_refresh_rate_diff = static_cast<int>(dst->refresh_rate) - static_cast<int>(refresh_rate);

  for (i = 0; i < hdmi_modes_.size(); i++) {
    msm_hdmi_mode_timing_info *timing_mode = &supported_video_modes_[i];
    if (cur->active_h == timing_mode->active_h &&
       cur->active_v == timing_mode->active_v &&
       cur->pixel_formats == timing_mode->pixel_formats ) {
      int cur_refresh_rate_diff = static_cast<int>(timing_mode->refresh_rate) -
                                  static_cast<int>(refresh_rate);
      if (abs(pre_refresh_rate_diff) > abs(cur_refresh_rate_diff)) {
        pre_refresh_rate_diff = cur_refresh_rate_diff;
        dst = timing_mode;
      }
    }
  }

  if (pre_refresh_rate_diff > kThresholdRefreshRate) {
    return kErrorNotSupported;
  }

  GetConfigIndex(dst->video_format, config_index);

  data->hor_front_porch = dst->front_porch_h;
  data->hor_back_porch = dst->back_porch_h;
  data->hor_pulse_width = dst->pulse_width_h;
  data->clk_rate_hz = dst->pixel_freq;
  data->fps = refresh_rate;

  if (dst->front_porch_h != cur->front_porch_h) {
    *mode = kModeHFP;
  }

  if (dst->refresh_rate != refresh_rate || dst->pixel_freq != cur->pixel_freq) {
    if (*mode == kModeHFP) {
      if (dst->refresh_rate != refresh_rate) {
        *mode = kModeHFPCalcClock;
      } else {
        *mode = kModeClockHFP;
      }
    } else {
        *mode = kModeClock;
    }
  }

  if (pre_unstd_mode && (*mode == kModeHFP)) {
    *mode = kModeClockHFP;
  }

  return kErrorNone;
}

DisplayError HWHDMI::SetRefreshRate(uint32_t refresh_rate) {
  char mode_path[kMaxStringLength] = {0};
  char node_path[kMaxStringLength] = {0};
  uint32_t mode = kModeClock;
  uint32_t config_index = 0;
  DynamicFPSData data;
  DisplayError error = kErrorNone;

  if (refresh_rate == frame_rate_) {
    return error;
  }

  error = GetDynamicFrameRateMode(refresh_rate, &mode, &data, &config_index);
  if (error != kErrorNone) {
    return error;
  }

  snprintf(mode_path, sizeof(mode_path), "%s%d/msm_fb_dfps_mode", fb_path_, fb_node_index_);
  snprintf(node_path, sizeof(node_path), "%s%d/dynamic_fps", fb_path_, fb_node_index_);

  int fd_mode = Sys::open_(mode_path, O_WRONLY);
  if (fd_mode < 0) {
    DLOGE("Failed to open %s with error %s", mode_path, strerror(errno));
    return kErrorFileDescriptor;
  }

  char dfps_mode[kMaxStringLength];
  snprintf(dfps_mode, sizeof(dfps_mode), "%d", mode);
  DLOGI_IF(kTagDriverConfig, "Setting dfps_mode  = %d", mode);
  ssize_t len = Sys::pwrite_(fd_mode, dfps_mode, strlen(dfps_mode), 0);
  if (len < 0) {
    DLOGE("Failed to enable dfps mode %d with error %s", mode, strerror(errno));
    Sys::close_(fd_mode);
    return kErrorUndefined;
  }
  Sys::close_(fd_mode);

  int fd_node = Sys::open_(node_path, O_WRONLY);
  if (fd_node < 0) {
    DLOGE("Failed to open %s with error %s", node_path, strerror(errno));
    return kErrorFileDescriptor;
  }

  char refresh_rate_string[kMaxStringLength];
  if (mode == kModeHFP || mode == kModeClock) {
    snprintf(refresh_rate_string, sizeof(refresh_rate_string), "%d", data.fps);
    DLOGI_IF(kTagDriverConfig, "Setting refresh rate = %d", data.fps);
  } else {
    snprintf(refresh_rate_string, sizeof(refresh_rate_string), "%d %d %d %d %d",
             data.hor_front_porch, data.hor_back_porch, data.hor_pulse_width,
             data.clk_rate_hz, data.fps);
  }
  len = Sys::pwrite_(fd_node, refresh_rate_string, strlen(refresh_rate_string), 0);
  if (len < 0) {
    DLOGE("Failed to write %d with error %s", refresh_rate, strerror(errno));
    Sys::close_(fd_node);
    return kErrorUndefined;
  }
  Sys::close_(fd_node);

  error = ReadTimingInfo();
  if (error != kErrorNone) {
    return error;
  }

  GetDisplayAttributes(config_index, &display_attributes_);
  UpdateMixerAttributes();

  frame_rate_ = refresh_rate;
  active_config_index_ = config_index;

  DLOGI_IF(kTagDriverConfig, "config_index(%d) Mode(%d) frame_rate(%d)",
           config_index,
           mode,
           frame_rate_);

  return kErrorNone;
}

void HWHDMI::UpdateMixerAttributes() {
  mixer_attributes_.width = display_attributes_.x_pixels;
  mixer_attributes_.height = display_attributes_.y_pixels;
  mixer_attributes_.split_left = display_attributes_.is_device_split ?
      (display_attributes_.x_pixels / 2) : mixer_attributes_.width;
}

DisplayError HWHDMI::UpdateHDRMetaData(HWLayers *hw_layers) {
  if (!hw_panel_info_.hdr_enabled) {
    return kErrorNone;
  }

  DisplayError error = kErrorNone;

#ifdef MDP_HDR_STREAM
  const HWHDRLayerInfo &hdr_layer_info = hw_layers->info.hdr_layer_info;
  char hdr_stream_path[kMaxStringLength] = {};
  snprintf(hdr_stream_path, sizeof(hdr_stream_path), "%s%d/hdr_stream", fb_path_, fb_node_index_);

  Layer hdr_layer = {};
  if (hdr_layer_info.operation == HWHDRLayerInfo::kSet && hdr_layer_info.layer_index > -1) {
    hdr_layer = *(hw_layers->info.stack->layers.at(UINT32(hdr_layer_info.layer_index)));
  }

  const LayerBuffer *layer_buffer = &hdr_layer.input_buffer;
  const MasteringDisplay &mastering_display = layer_buffer->color_metadata.masteringDisplayInfo;
  const ContentLightLevel &light_level = layer_buffer->color_metadata.contentLightLevel;
  const Primaries &primaries = mastering_display.primaries;

  mdp_hdr_stream_ctrl hdr_ctrl = {};
  if (hdr_layer_info.operation == HWHDRLayerInfo::kSet) {
    int32_t eotf = GetEOTF(layer_buffer->color_metadata.transfer);
    hdr_ctrl.hdr_stream.eotf = (eotf < 0) ? 0 : UINT32(eotf);
    hdr_ctrl.hdr_stream.white_point_x = primaries.whitePoint[0];
    hdr_ctrl.hdr_stream.white_point_y = primaries.whitePoint[1];
    hdr_ctrl.hdr_stream.display_primaries_x[0] = primaries.rgbPrimaries[0][0];
    hdr_ctrl.hdr_stream.display_primaries_y[0] = primaries.rgbPrimaries[0][1];
    hdr_ctrl.hdr_stream.display_primaries_x[1] = primaries.rgbPrimaries[1][0];
    hdr_ctrl.hdr_stream.display_primaries_y[1] = primaries.rgbPrimaries[1][1];
    hdr_ctrl.hdr_stream.display_primaries_x[2] = primaries.rgbPrimaries[2][0];
    hdr_ctrl.hdr_stream.display_primaries_y[2] = primaries.rgbPrimaries[2][1];
    hdr_ctrl.hdr_stream.min_luminance = mastering_display.minDisplayLuminance;
    hdr_ctrl.hdr_stream.max_luminance = mastering_display.maxDisplayLuminance/10000;
    hdr_ctrl.hdr_stream.max_content_light_level = light_level.maxContentLightLevel;
    hdr_ctrl.hdr_stream.max_average_light_level = light_level.minPicAverageLightLevel;
    hdr_ctrl.hdr_state = HDR_ENABLE;
    reset_hdr_flag_ = false;
#ifdef MDP_COMMIT_UPDATE_CDM_COLOR_SPACE
    HWDevice::SetCSC(layer_buffer->color_metadata, &cdm_color_space_);
    cdm_color_space_commit_ = true;
#endif
    // DP related
    int32_t pixel_encoding = GetPixelEncoding(hdr_layer.input_buffer);
    hdr_ctrl.hdr_stream.pixel_encoding = (pixel_encoding < 0) ? 0 : UINT32(pixel_encoding);
    int32_t colorimetry = GetColoriMetry(hdr_layer.input_buffer);
    hdr_ctrl.hdr_stream.colorimetry = (colorimetry < 0) ? 0 : UINT32(colorimetry);
    hdr_ctrl.hdr_stream.range = GetRange(hdr_layer.input_buffer.color_metadata.range);
    int32_t bits_per_component = GetBitsPerComponent(hdr_layer.input_buffer);
    hdr_ctrl.hdr_stream.bits_per_component =
                           (bits_per_component  < 0) ? 0 : UINT32(bits_per_component);
    hdr_ctrl.hdr_stream.content_type = GetContentType(hdr_layer.input_buffer);

    DLOGD_IF(kTagDriverConfig, "kSet: HDR Stream : MaxDisplayLuminance = %d\n"
      "MinDisplayLuminance = %d MaxContentLightLevel = %d MaxAverageLightLevel = %d\n"
      "Red_x = %d Red_y = %d Green_x = %d Green_y = %d Blue_x = %d Blue_y = %d\n"
      "WhitePoint_x = %d WhitePoint_y = %d EOTF = %d PixelEncoding = %d Colorimetry = %d\n"
      "Range = %d BPC = %d ContentType = %d hdr_state = %d",
      hdr_ctrl.hdr_stream.max_luminance, hdr_ctrl.hdr_stream.min_luminance,
      hdr_ctrl.hdr_stream.max_content_light_level, hdr_ctrl.hdr_stream.max_average_light_level,
      hdr_ctrl.hdr_stream.display_primaries_x[0], hdr_ctrl.hdr_stream.display_primaries_y[0],
      hdr_ctrl.hdr_stream.display_primaries_x[1], hdr_ctrl.hdr_stream.display_primaries_y[1],
      hdr_ctrl.hdr_stream.display_primaries_x[2], hdr_ctrl.hdr_stream.display_primaries_y[2],
      hdr_ctrl.hdr_stream.white_point_x, hdr_ctrl.hdr_stream.white_point_x,
      hdr_ctrl.hdr_stream.eotf, hdr_ctrl.hdr_stream.pixel_encoding,
      hdr_ctrl.hdr_stream.colorimetry, hdr_ctrl.hdr_stream.range,
      hdr_ctrl.hdr_stream.bits_per_component, hdr_ctrl.hdr_stream.content_type,
      hdr_ctrl.hdr_state);
  } else if (hdr_layer_info.operation == HWHDRLayerInfo::kReset) {
    memset(&hdr_ctrl.hdr_stream, 0, sizeof(hdr_ctrl.hdr_stream));
    hdr_ctrl.hdr_state = HDR_RESET;
    reset_hdr_flag_ = true;
    hdr_reset_start_ = time(NULL);
#ifdef MDP_COMMIT_UPDATE_CDM_COLOR_SPACE
    cdm_color_space_ = (mdp_color_space) MDP_CSC_DEFAULT;
    cdm_color_space_commit_ = true;
#endif
    DLOGD_IF(kTagDriverConfig, "kReset: HDR Stream: HDR_RESET");
  } else if (hdr_layer_info.operation == HWHDRLayerInfo::kNoOp) {
     if (reset_hdr_flag_) {
       hdr_reset_end_ = time(NULL);

       if ((hdr_reset_end_ - hdr_reset_start_) >= MIN_HDR_RESET_WAITTIME_SEC) {
          reset_hdr_flag_ = false;
          memset(&hdr_ctrl.hdr_stream, 0, sizeof(hdr_ctrl.hdr_stream));
          hdr_ctrl.hdr_state = HDR_DISABLE;
          DLOGD_IF(kTagDriverConfig, "kNoOp: HDR Stream: HDR_DISABLE");
       } else {
          return kErrorNone;
       }
     } else {
        return kErrorNone;
     }
  }

  int fd = Sys::open_(hdr_stream_path, O_WRONLY);
  if (fd < 0) {
    DLOGE("Failed to open %s with error %s", hdr_stream_path, strerror(errno));
    return kErrorFileDescriptor;
  }

  const void *hdr_metadata = reinterpret_cast<const void*>(&hdr_ctrl);
  ssize_t len = Sys::pwrite_(fd, hdr_metadata, sizeof(hdr_ctrl), 0);
  if (len <= 0) {
    DLOGE("Failed to write hdr_metadata");
    error = kErrorUndefined;
  }
  Sys::close_(fd);
#endif

  return error;
}

DisplayError HWHDMI::GetHdmiMode(std::vector<uint32_t> &hdmi_modes) {
  hdmi_modes = hdmi_modes_;
  return kErrorNone;
}


}  // namespace sdm

