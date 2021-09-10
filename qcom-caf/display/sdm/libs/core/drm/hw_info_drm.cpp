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

#include <dlfcn.h>
#include <drm/drm_fourcc.h>
#include <drm_lib_loader.h>
#include <drm_master.h>
#include <drm_res_mgr.h>
#include <fcntl.h>
#include <media/msm_sde_rotator.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <utils/constants.h>
#include <utils/debug.h>
#include <utils/sys.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "hw_info_drm.h"

#ifndef DRM_FORMAT_MOD_QCOM_COMPRESSED
#define DRM_FORMAT_MOD_QCOM_COMPRESSED fourcc_mod_code(QCOM, 1)
#endif
#ifndef DRM_FORMAT_MOD_QCOM_DX
#define DRM_FORMAT_MOD_QCOM_DX fourcc_mod_code(QCOM, 0x2)
#endif
#ifndef DRM_FORMAT_MOD_QCOM_TIGHT
#define DRM_FORMAT_MOD_QCOM_TIGHT fourcc_mod_code(QCOM, 0x4)
#endif

#define __CLASS__ "HWInfoDRM"

using drm_utils::DRMMaster;
using drm_utils::DRMResMgr;
using drm_utils::DRMLogger;
using drm_utils::DRMLibLoader;
using sde_drm::GetDRMManager;
using sde_drm::DRMPlanesInfo;
using sde_drm::DRMCrtcInfo;
using sde_drm::DRMPlaneType;

using std::vector;
using std::map;
using std::string;
using std::fstream;
using std::to_string;

namespace sdm {

class DRMLoggerImpl : public DRMLogger {
 public:
#define PRINTLOG(tag, method, format, buf)        \
  va_list list;                              \
  va_start(list, format);                    \
  vsnprintf(buf, sizeof(buf), format, list); \
  va_end(list);                              \
  Debug::Get()->method(tag, "%s", buf);

  void Error(const char *format, ...) { PRINTLOG(kTagNone, Error, format, buf_); }
  void Warning(const char *format, ...) { PRINTLOG(kTagDriverConfig, Warning, format, buf_); }
  void Info(const char *format, ...) { PRINTLOG(kTagDriverConfig, Info, format, buf_); }
  void Debug(const char *format, ...) { PRINTLOG(kTagDriverConfig, Debug, format, buf_); }
  void Verbose(const char *format, ...) { PRINTLOG(kTagDriverConfig, Verbose, format, buf_); }

 private:
  char buf_[1024] = {};
};

HWResourceInfo *HWInfoDRM::hw_resource_ = nullptr;

HWInfoDRM::HWInfoDRM() {
  DRMLogger::Set(new DRMLoggerImpl());
  drm_lib = DRMLibLoader::GetInstance();
  if (drm_lib == nullptr) {
    DLOGE("Failed to load DRM Library");
    return;
  }
  default_mode_ = (drm_lib->IsLoaded() == false);
  if (!default_mode_) {
    DRMMaster *drm_master = {};
    int dev_fd = -1;
    DRMMaster::GetInstance(&drm_master);
    if (!drm_master) {
      DLOGE("Failed to acquire DRMMaster instance");
      return;
    }
    drm_master->GetHandle(&dev_fd);
    drm_lib->FuncGetDRMManager()(dev_fd, &drm_mgr_intf_);
  }
}

HWInfoDRM::~HWInfoDRM() {
  if (hw_resource_ != nullptr) {
    delete hw_resource_;
    hw_resource_ = nullptr;
  }

  if (drm_mgr_intf_) {
    if (drm_lib != nullptr) {
      drm_lib->FuncDestroyDRMManager()();
    }
    drm_mgr_intf_ = nullptr;
  }

  drm_lib->Destroy();
  drm_lib = nullptr;
  DRMMaster::DestroyInstance();
}

DisplayError HWInfoDRM::GetDynamicBWLimits(HWResourceInfo *hw_resource) {
  HWDynBwLimitInfo* bw_info = &hw_resource->dyn_bw_info;
  for (int index = 0; index < kBwModeMax; index++) {
    bw_info->total_bw_limit[index] = UINT32(hw_resource->max_bandwidth_low);
    bw_info->pipe_bw_limit[index] = hw_resource->max_pipe_bw;
  }

  return kErrorNone;
}

DisplayError HWInfoDRM::GetHWResourceInfo(HWResourceInfo *hw_resource) {
  if (hw_resource_) {
    *hw_resource = *hw_resource_;
    return kErrorNone;
  }

  hw_resource->num_blending_stages = 1;
  hw_resource->max_pipe_width = 2560;
  hw_resource->max_cursor_size = 128;
  hw_resource->max_scale_down = 1;
  hw_resource->max_scale_up = 1;
  hw_resource->has_decimation = false;
  hw_resource->max_bandwidth_low = 9600000;
  hw_resource->max_bandwidth_high = 9600000;
  hw_resource->max_pipe_bw = 4500000;
  hw_resource->max_sde_clk = 412500000;
  hw_resource->clk_fudge_factor = FLOAT(105) / FLOAT(100);
  hw_resource->macrotile_nv12_factor = 8;
  hw_resource->macrotile_factor = 4;
  hw_resource->linear_factor = 1;
  hw_resource->scale_factor = 1;
  hw_resource->extra_fudge_factor = 2;
  hw_resource->amortizable_threshold = 0;
  hw_resource->system_overhead_lines = 0;
  hw_resource->hw_dest_scalar_info.count = 0;
  hw_resource->hw_dest_scalar_info.max_scale_up = 0;
  hw_resource->hw_dest_scalar_info.max_input_width = 0;
  hw_resource->hw_dest_scalar_info.max_output_width = 0;
  hw_resource->is_src_split = true;
  hw_resource->perf_calc = false;
  hw_resource->has_dyn_bw_support = false;
  hw_resource->has_qseed3 = false;
  hw_resource->has_concurrent_writeback = false;

  // TODO(user): Deprecate
  hw_resource->hw_version = kHWMdssVersion5;
  hw_resource->hw_revision = 0;

  // TODO(user): Deprecate
  hw_resource->max_mixer_width = 2560;
  hw_resource->writeback_index = 0;
  hw_resource->has_bwc = false;
  hw_resource->has_ubwc = true;
  hw_resource->has_macrotile = true;
  hw_resource->separate_rotator = true;
  hw_resource->has_non_scalar_rgb = false;

  GetSystemInfo(hw_resource);
  GetHWPlanesInfo(hw_resource);
  GetWBInfo(hw_resource);

  // Disable destination scalar count to 0 if extension library is not present
  DynLib extension_lib;
  if (!extension_lib.Open("libsdmextension.so")) {
    hw_resource->hw_dest_scalar_info.count = 0;
  }

  DLOGI("Max plane width = %d", hw_resource->max_pipe_width);
  DLOGI("Max cursor width = %d", hw_resource->max_cursor_size);
  DLOGI("Max plane upscale = %d", hw_resource->max_scale_up);
  DLOGI("Max plane downscale = %d", hw_resource->max_scale_down);
  DLOGI("Has Decimation = %d", hw_resource->has_decimation);
  DLOGI("Max Blending Stages = %d", hw_resource->num_blending_stages);
  DLOGI("Has Source Split = %d", hw_resource->is_src_split);
  DLOGI("Has QSEED3 = %d", hw_resource->has_qseed3);
  DLOGI("Has UBWC = %d", hw_resource->has_ubwc);
  DLOGI("Has Concurrent Writeback = %d", hw_resource->has_concurrent_writeback);
  DLOGI("Max Low Bw = %" PRIu64 "", hw_resource->max_bandwidth_low);
  DLOGI("Max High Bw = % " PRIu64 "", hw_resource->max_bandwidth_high);
  DLOGI("Max Pipe Bw = %" PRIu64 " KBps", hw_resource->max_pipe_bw);
  DLOGI("MaxSDEClock = % " PRIu64 " Hz", hw_resource->max_sde_clk);
  DLOGI("Clock Fudge Factor = %f", hw_resource->clk_fudge_factor);
  DLOGI("Prefill factors:");
  DLOGI("\tTiled_NV12 = %d", hw_resource->macrotile_nv12_factor);
  DLOGI("\tTiled = %d", hw_resource->macrotile_factor);
  DLOGI("\tLinear = %d", hw_resource->linear_factor);
  DLOGI("\tScale = %d", hw_resource->scale_factor);
  DLOGI("\tFudge_factor = %d", hw_resource->extra_fudge_factor);

  if (hw_resource->separate_rotator || hw_resource->num_dma_pipe) {
    GetHWRotatorInfo(hw_resource);
  }

  if (hw_resource->has_dyn_bw_support) {
    DisplayError ret = GetDynamicBWLimits(hw_resource);
    if (ret != kErrorNone) {
      DLOGE("Failed to read dynamic band width info");
      return ret;
    }

    DLOGI("Has Support for multiple bw limits shown below");
    for (int index = 0; index < kBwModeMax; index++) {
      DLOGI("Mode-index=%d  total_bw_limit=%d and pipe_bw_limit=%d", index,
            hw_resource->dyn_bw_info.total_bw_limit[index],
            hw_resource->dyn_bw_info.pipe_bw_limit[index]);
    }
  }

  if (!hw_resource_) {
    hw_resource_ = new HWResourceInfo();
    *hw_resource_ = *hw_resource;
  }

  return kErrorNone;
}

void HWInfoDRM::GetSystemInfo(HWResourceInfo *hw_resource) {
  DRMCrtcInfo info;
  drm_mgr_intf_->GetCrtcInfo(0 /* system_info */, &info);
  hw_resource->is_src_split = info.has_src_split;
  hw_resource->has_qseed3 = (info.qseed_version == sde_drm::QSEEDVersion::V3);
  hw_resource->num_blending_stages = info.max_blend_stages;
  hw_resource->smart_dma_rev = (info.smart_dma_rev == sde_drm::SmartDMARevision::V2) ?
    SmartDMARevision::V2 : SmartDMARevision::V1;
}

void HWInfoDRM::GetHWPlanesInfo(HWResourceInfo *hw_resource) {
  DRMPlanesInfo planes;
  drm_mgr_intf_->GetPlanesInfo(&planes);
  for (auto &pipe_obj : planes) {
    HWPipeCaps pipe_caps;
    string name = {};
    switch (pipe_obj.second.type) {
      case DRMPlaneType::DMA:
        name = "DMA";
        pipe_caps.type = kPipeTypeDMA;
        if (!hw_resource->num_dma_pipe) {
          PopulateSupportedFmts(kHWDMAPipe, pipe_obj.second, hw_resource);
        }
        hw_resource->num_dma_pipe++;
        break;
      case DRMPlaneType::VIG:
        name = "VIG";
        pipe_caps.type = kPipeTypeVIG;
        if (!hw_resource->num_vig_pipe) {
          PopulatePipeCaps(pipe_obj.second, hw_resource);
          PopulateSupportedFmts(kHWVIGPipe, pipe_obj.second, hw_resource);
        }
        hw_resource->num_vig_pipe++;
        break;
      case DRMPlaneType::CURSOR:
        name = "CURSOR";
        pipe_caps.type = kPipeTypeCursor;
        if (!hw_resource->num_cursor_pipe) {
          PopulateSupportedFmts(kHWCursorPipe, pipe_obj.second, hw_resource);
          hw_resource->max_cursor_size = pipe_obj.second.max_linewidth;
        }
        hw_resource->num_cursor_pipe++;
        break;
      default:
        continue;  // Not adding any other pipe type
    }
    pipe_caps.id = pipe_obj.first;
    pipe_caps.master_pipe_id = pipe_obj.second.master_plane_id;
    DLOGI("Adding %s Pipe : Id %d", name.c_str(), pipe_obj.first);
    hw_resource->hw_pipes.push_back(std::move(pipe_caps));
  }
}

void HWInfoDRM::PopulatePipeCaps(const sde_drm::DRMPlaneTypeInfo &info,
                                    HWResourceInfo *hw_resource) {
  hw_resource->max_pipe_width = info.max_linewidth;
  hw_resource->max_scale_down = info.max_downscale;
  hw_resource->max_scale_up = info.max_upscale;
  hw_resource->has_decimation = info.max_horizontal_deci > 1 && info.max_vertical_deci > 1;
}

void HWInfoDRM::PopulateSupportedFmts(HWSubBlockType sub_blk_type,
                                      const sde_drm::DRMPlaneTypeInfo  &info,
                                      HWResourceInfo *hw_resource) {
  vector<LayerBufferFormat> sdm_formats;
  FormatsMap &fmts_map = hw_resource->supported_formats_map;

  if (fmts_map.find(sub_blk_type) == fmts_map.end()) {
    for (auto &fmts : info.formats_supported) {
      GetSDMFormat(fmts.first, fmts.second, &sdm_formats);
    }

    fmts_map.insert(make_pair(sub_blk_type, sdm_formats));
  }
}

void HWInfoDRM::GetWBInfo(HWResourceInfo *hw_resource) {
  HWSubBlockType sub_blk_type = kHWWBIntfOutput;
  vector<LayerBufferFormat> supported_sdm_formats;
  sde_drm::DRMDisplayToken token;

  // Fake register
  if (drm_mgr_intf_->RegisterDisplay(sde_drm::DRMDisplayType::VIRTUAL, &token)) {
    return;
  }

  sde_drm::DRMConnectorInfo connector_info;
  drm_mgr_intf_->GetConnectorInfo(token.conn_id, &connector_info);
  for (auto &fmts : connector_info.formats_supported) {
    GetSDMFormat(fmts.first, fmts.second, &supported_sdm_formats);
  }

  hw_resource->supported_formats_map.erase(sub_blk_type);
  hw_resource->supported_formats_map.insert(make_pair(sub_blk_type, supported_sdm_formats));

  drm_mgr_intf_->UnregisterDisplay(token);
}

void HWInfoDRM::GetSDMFormat(uint32_t v4l2_format, LayerBufferFormat *sdm_format) {
  switch (v4l2_format) {
    case SDE_PIX_FMT_ARGB_8888:         *sdm_format = kFormatARGB8888;                 break;
    case SDE_PIX_FMT_RGBA_8888:         *sdm_format = kFormatRGBA8888;                 break;
    case SDE_PIX_FMT_BGRA_8888:         *sdm_format = kFormatBGRA8888;                 break;
    case SDE_PIX_FMT_RGBX_8888:         *sdm_format = kFormatRGBX8888;                 break;
    case SDE_PIX_FMT_BGRX_8888:         *sdm_format = kFormatBGRX8888;                 break;
    case SDE_PIX_FMT_RGBA_5551:         *sdm_format = kFormatRGBA5551;                 break;
    case SDE_PIX_FMT_RGBA_4444:         *sdm_format = kFormatRGBA4444;                 break;
    case SDE_PIX_FMT_RGB_888:           *sdm_format = kFormatRGB888;                   break;
    case SDE_PIX_FMT_BGR_888:           *sdm_format = kFormatBGR888;                   break;
    case SDE_PIX_FMT_RGB_565:           *sdm_format = kFormatRGB565;                   break;
    case SDE_PIX_FMT_BGR_565:           *sdm_format = kFormatBGR565;                   break;
    case SDE_PIX_FMT_Y_CB_CR_H2V2:      *sdm_format = kFormatYCbCr420Planar;           break;
    case SDE_PIX_FMT_Y_CR_CB_H2V2:      *sdm_format = kFormatYCrCb420Planar;           break;
    case SDE_PIX_FMT_Y_CR_CB_GH2V2:     *sdm_format = kFormatYCrCb420PlanarStride16;   break;
    case SDE_PIX_FMT_Y_CBCR_H2V2:       *sdm_format = kFormatYCbCr420SemiPlanar;       break;
    case SDE_PIX_FMT_Y_CRCB_H2V2:       *sdm_format = kFormatYCrCb420SemiPlanar;       break;
    case SDE_PIX_FMT_Y_CBCR_H1V2:       *sdm_format = kFormatYCbCr422H1V2SemiPlanar;   break;
    case SDE_PIX_FMT_Y_CRCB_H1V2:       *sdm_format = kFormatYCrCb422H1V2SemiPlanar;   break;
    case SDE_PIX_FMT_Y_CBCR_H2V1:       *sdm_format = kFormatYCbCr422H2V1SemiPlanar;   break;
    case SDE_PIX_FMT_Y_CRCB_H2V1:       *sdm_format = kFormatYCrCb422H2V1SemiPlanar;   break;
    case SDE_PIX_FMT_YCBYCR_H2V1:       *sdm_format = kFormatYCbCr422H2V1Packed;       break;
    case SDE_PIX_FMT_Y_CBCR_H2V2_VENUS: *sdm_format = kFormatYCbCr420SemiPlanarVenus;  break;
    case SDE_PIX_FMT_Y_CRCB_H2V2_VENUS: *sdm_format = kFormatYCrCb420SemiPlanarVenus;  break;
    case SDE_PIX_FMT_RGBA_8888_UBWC:    *sdm_format = kFormatRGBA8888Ubwc;             break;
    case SDE_PIX_FMT_RGBX_8888_UBWC:    *sdm_format = kFormatRGBX8888Ubwc;             break;
    case SDE_PIX_FMT_RGB_565_UBWC:      *sdm_format = kFormatBGR565Ubwc;               break;
    case SDE_PIX_FMT_Y_CBCR_H2V2_UBWC:  *sdm_format = kFormatYCbCr420SPVenusUbwc;      break;
    case SDE_PIX_FMT_RGBA_1010102:      *sdm_format = kFormatRGBA1010102;              break;
    case SDE_PIX_FMT_ARGB_2101010:      *sdm_format = kFormatARGB2101010;              break;
    case SDE_PIX_FMT_RGBX_1010102:      *sdm_format = kFormatRGBX1010102;              break;
    case SDE_PIX_FMT_XRGB_2101010:      *sdm_format = kFormatXRGB2101010;              break;
    case SDE_PIX_FMT_BGRA_1010102:      *sdm_format = kFormatBGRA1010102;              break;
    case SDE_PIX_FMT_ABGR_2101010:      *sdm_format = kFormatABGR2101010;              break;
    case SDE_PIX_FMT_BGRX_1010102:      *sdm_format = kFormatBGRX1010102;              break;
    case SDE_PIX_FMT_XBGR_2101010:      *sdm_format = kFormatXBGR2101010;              break;
    case SDE_PIX_FMT_RGBA_1010102_UBWC: *sdm_format = kFormatRGBA1010102Ubwc;          break;
    case SDE_PIX_FMT_RGBX_1010102_UBWC: *sdm_format = kFormatRGBX1010102Ubwc;          break;
    case SDE_PIX_FMT_Y_CBCR_H2V2_P010:  *sdm_format = kFormatYCbCr420P010;             break;
    case SDE_PIX_FMT_Y_CBCR_H2V2_TP10_UBWC: *sdm_format = kFormatYCbCr420TP10Ubwc;     break;
    /* TODO(user) : enable when defined in uapi
      case SDE_PIX_FMT_Y_CBCR_H2V2_P010_UBWC: *sdm_format = kFormatYCbCr420P010Ubwc;     break; */
    default: *sdm_format = kFormatInvalid;
  }
}

void HWInfoDRM::GetRotatorFormatsForType(int fd, uint32_t type,
                                         vector<LayerBufferFormat> *supported_formats) {
  struct v4l2_fmtdesc fmtdesc = {};
  fmtdesc.type = type;
  while (!Sys::ioctl_(fd, static_cast<int>(VIDIOC_ENUM_FMT), &fmtdesc)) {
    LayerBufferFormat sdm_format = kFormatInvalid;
    GetSDMFormat(fmtdesc.pixelformat, &sdm_format);
    if (sdm_format != kFormatInvalid) {
      supported_formats->push_back(sdm_format);
    }
    fmtdesc.index++;
  }
}

DisplayError HWInfoDRM::GetRotatorSupportedFormats(uint32_t v4l2_index,
                                                   HWResourceInfo *hw_resource) {
  string path = "/dev/video" + to_string(v4l2_index);
  int fd = Sys::open_(path.c_str(), O_RDONLY);
  if (fd < 0) {
    DLOGE("Failed to open %s with error %d", path.c_str(), errno);
    return kErrorNotSupported;
  }

  vector<LayerBufferFormat> supported_formats = {};
  GetRotatorFormatsForType(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT, &supported_formats);
  hw_resource->supported_formats_map.erase(kHWRotatorInput);
  hw_resource->supported_formats_map.insert(make_pair(kHWRotatorInput, supported_formats));

  supported_formats = {};
  GetRotatorFormatsForType(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE, &supported_formats);
  hw_resource->supported_formats_map.erase(kHWRotatorOutput);
  hw_resource->supported_formats_map.insert(make_pair(kHWRotatorOutput, supported_formats));

  Sys::close_(fd);

  return kErrorNone;
}

DisplayError HWInfoDRM::GetHWRotatorInfo(HWResourceInfo *hw_resource) {
  string v4l2_path = "/sys/class/video4linux/video";
  const uint32_t kMaxV4L2Nodes = 64;

  for (uint32_t i = 0; i < kMaxV4L2Nodes; i++) {
    string path = v4l2_path + to_string(i) + "/name";
    Sys::fstream fs(path, fstream::in);
    if (!fs.is_open()) {
      continue;
    }

    string line;
    if (Sys::getline_(fs, line) && (!strncmp(line.c_str(), "sde_rotator", strlen("sde_rotator")))) {
      hw_resource->hw_rot_info.device_path = string("/dev/video" + to_string(i));
      hw_resource->hw_rot_info.num_rotator++;
      hw_resource->hw_rot_info.type = HWRotatorInfo::ROT_TYPE_V4L2;
      hw_resource->hw_rot_info.has_downscale = true;
      GetRotatorSupportedFormats(i, hw_resource);

      string caps_path = v4l2_path + to_string(i) + "/device/caps";
      Sys::fstream caps_fs(caps_path, fstream::in);

      if (caps_fs.is_open()) {
        string caps;
        while (Sys::getline_(caps_fs, caps)) {
          const string downscale_compression = "downscale_compression=";
          const string min_downscale = "min_downscale=";
          if (caps.find(downscale_compression) != string::npos) {
            hw_resource->hw_rot_info.downscale_compression =
              std::stoi(string(caps, downscale_compression.length()));
          } else if (caps.find(min_downscale) != string::npos) {
            hw_resource->hw_rot_info.min_downscale =
              std::stof(string(caps, min_downscale.length()));
          }
        }
      }

      // We support only 1 rotator
      break;
    }
  }

  DLOGI("V4L2 Rotator: Count = %d, Downscale = %d, Min_downscale = %f, Downscale_compression = %d",
        hw_resource->hw_rot_info.num_rotator, hw_resource->hw_rot_info.has_downscale,
        hw_resource->hw_rot_info.min_downscale, hw_resource->hw_rot_info.downscale_compression);

  return kErrorNone;
}

void HWInfoDRM::GetSDMFormat(uint32_t drm_format, uint64_t drm_format_modifier,
                             vector<LayerBufferFormat> *sdm_formats) {
  vector<LayerBufferFormat> &fmts(*sdm_formats);
  switch (drm_format) {
    case DRM_FORMAT_BGRA8888:
      fmts.push_back(kFormatARGB8888);
      break;
    case DRM_FORMAT_ABGR8888:
      fmts.push_back(drm_format_modifier ? kFormatRGBA8888Ubwc : kFormatRGBA8888);
      break;
    case DRM_FORMAT_ARGB8888:
      fmts.push_back(kFormatBGRA8888);
      break;
    case DRM_FORMAT_BGRX8888:
      fmts.push_back(kFormatXRGB8888);
      break;
    case DRM_FORMAT_XBGR8888:
      fmts.push_back(drm_format_modifier ? kFormatRGBX8888Ubwc : kFormatRGBX8888);
      break;
    case DRM_FORMAT_XRGB8888:
      fmts.push_back(kFormatBGRX8888);
      break;
    case DRM_FORMAT_ABGR1555:
      fmts.push_back(kFormatRGBA5551);
      break;
    case DRM_FORMAT_ABGR4444:
      fmts.push_back(kFormatRGBA4444);
      break;
    case DRM_FORMAT_BGR888:
      fmts.push_back(kFormatRGB888);
      break;
    case DRM_FORMAT_RGB888:
      fmts.push_back(kFormatBGR888);
      break;
    case DRM_FORMAT_BGR565:
      fmts.push_back(drm_format_modifier ? kFormatBGR565Ubwc : kFormatRGB565);
      break;
    case DRM_FORMAT_RGB565:
      fmts.push_back(kFormatBGR565);
      break;
    case DRM_FORMAT_ABGR2101010:
      fmts.push_back(drm_format_modifier ? kFormatRGBA1010102Ubwc : kFormatRGBA1010102);
      break;
    case DRM_FORMAT_BGRA1010102:
      fmts.push_back(kFormatARGB2101010);
      break;
    case DRM_FORMAT_XBGR2101010:
      fmts.push_back(drm_format_modifier ? kFormatRGBX1010102Ubwc : kFormatRGBX1010102);
      break;
    case DRM_FORMAT_BGRX1010102:
      fmts.push_back(kFormatXRGB2101010);
      break;
    case DRM_FORMAT_ARGB2101010:
      fmts.push_back(kFormatBGRA1010102);
      break;
    case DRM_FORMAT_RGBA1010102:
      fmts.push_back(kFormatABGR2101010);
      break;
    case DRM_FORMAT_XRGB2101010:
      fmts.push_back(kFormatBGRX1010102);
      break;
    case DRM_FORMAT_RGBX1010102:
      fmts.push_back(kFormatXBGR2101010);
      break;
    case DRM_FORMAT_YVU420:
      fmts.push_back(kFormatYCrCb420PlanarStride16);
      break;
    case DRM_FORMAT_NV12:
      if (drm_format_modifier == (DRM_FORMAT_MOD_QCOM_COMPRESSED |
          DRM_FORMAT_MOD_QCOM_DX | DRM_FORMAT_MOD_QCOM_TIGHT)) {
          fmts.push_back(kFormatYCbCr420TP10Ubwc);
      } else if (drm_format_modifier == (DRM_FORMAT_MOD_QCOM_COMPRESSED |
                                         DRM_FORMAT_MOD_QCOM_DX)) {
        fmts.push_back(kFormatYCbCr420P010Ubwc);
      } else if (drm_format_modifier == DRM_FORMAT_MOD_QCOM_COMPRESSED) {
         fmts.push_back(kFormatYCbCr420SPVenusUbwc);
      } else if (drm_format_modifier == DRM_FORMAT_MOD_QCOM_DX) {
         fmts.push_back(kFormatYCbCr420P010);
      } else {
         fmts.push_back(kFormatYCbCr420SemiPlanarVenus);
         fmts.push_back(kFormatYCbCr420SemiPlanar);
      }
      break;
    case DRM_FORMAT_NV21:
      fmts.push_back(kFormatYCrCb420SemiPlanarVenus);
      fmts.push_back(kFormatYCrCb420SemiPlanar);
      break;
    case DRM_FORMAT_NV16:
      fmts.push_back(kFormatYCbCr422H2V1SemiPlanar);
      break;
    default:
      break;
  }
}

DisplayError HWInfoDRM::GetFirstDisplayInterfaceType(HWDisplayInterfaceInfo *hw_disp_info) {
  hw_disp_info->type = kPrimary;
  hw_disp_info->is_connected = true;

  return kErrorNone;
}

}  // namespace sdm
