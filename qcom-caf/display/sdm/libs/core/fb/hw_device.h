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

#ifndef __HW_DEVICE_H__
#define __HW_DEVICE_H__

#include <errno.h>
#include <linux/msm_mdp_ext.h>
#include <linux/mdss_rotator.h>
#include <pthread.h>
#include <vector>

#include "hw_interface.h"
#include "hw_scale.h"

#define IOCTL_LOGE(ioctl, type) DLOGE("ioctl %s, device = %d errno = %d, desc = %s", #ioctl, \
                                      type, errno, strerror(errno))

#ifndef MDP_LAYER_MULTIRECT_ENABLE
#define MDP_LAYER_MULTIRECT_ENABLE 0
#endif

#ifndef MDP_LAYER_MULTIRECT_PARALLEL_MODE
#define MDP_LAYER_MULTIRECT_PARALLEL_MODE 0
#endif

#ifndef MDP_LAYER_SECURE_CAMERA_SESSION
#define MDP_LAYER_SECURE_CAMERA_SESSION 0
#endif

namespace sdm {
class HWInfoInterface;

class HWDevice : public HWInterface {
 public:
  virtual ~HWDevice() {}
  virtual DisplayError Init();
  virtual DisplayError Deinit();

 protected:
  explicit HWDevice(BufferSyncHandler *buffer_sync_handler);

  // From HWInterface
  virtual DisplayError GetActiveConfig(uint32_t *active_config);
  virtual DisplayError GetNumDisplayAttributes(uint32_t *count);
  virtual DisplayError GetDisplayAttributes(uint32_t index,
                                            HWDisplayAttributes *display_attributes);
  virtual DisplayError GetHWPanelInfo(HWPanelInfo *panel_info);
  virtual DisplayError SetDisplayAttributes(uint32_t index);
  virtual DisplayError SetDisplayFormat(uint32_t index, DisplayInterfaceFormat fmt);
  virtual DisplayError SetDisplayAttributes(const HWDisplayAttributes &display_attributes);
  virtual DisplayError GetConfigIndex(uint32_t mode, uint32_t *index);
  virtual DisplayError GetConfigIndex(uint32_t width, uint32_t height, uint32_t *index);
  virtual DisplayError PowerOn();
  virtual DisplayError PowerOff();
  virtual DisplayError Doze();
  virtual DisplayError DozeSuspend();
  virtual DisplayError Standby();
  virtual DisplayError Validate(HWLayers *hw_layers);
  virtual DisplayError Commit(HWLayers *hw_layers);
  virtual DisplayError Flush(bool secure);
  virtual DisplayError GetPPFeaturesVersion(PPFeatureVersion *vers);
  virtual DisplayError SetPPFeatures(PPFeaturesConfig *feature_list);
  virtual DisplayError SetVSyncState(bool enable);
  virtual void SetIdleTimeoutMs(uint32_t timeout_ms);
  virtual DisplayError SetDisplayMode(const HWDisplayMode hw_display_mode);
  virtual DisplayError SetRefreshRate(uint32_t refresh_rate);
  virtual DisplayError SetPanelBrightness(int level);
  virtual DisplayError GetHWScanInfo(HWScanInfo *scan_info);
  virtual DisplayError GetVideoFormat(uint32_t config_index, uint32_t *video_format);
  virtual DisplayError GetMaxCEAFormat(uint32_t *max_cea_format);
  virtual DisplayError SetCursorPosition(HWLayers *hw_layers, int x, int y);
  virtual DisplayError OnMinHdcpEncryptionLevelChange(uint32_t min_enc_level);
  virtual DisplayError GetPanelBrightness(int *level);
  virtual DisplayError SetAutoRefresh(bool enable) { return kErrorNone; }
  virtual DisplayError SetS3DMode(HWS3DMode s3d_mode);
  virtual DisplayError SetScaleLutConfig(HWScaleLutInfo *lut_info);
  virtual DisplayError SetMixerAttributes(HWMixerAttributes &mixer_attributes);
  virtual DisplayError GetMixerAttributes(HWMixerAttributes *mixer_attributes);
  virtual DisplayError SetDynamicDSIClock(uint64_t bitclk);
  virtual DisplayError GetDynamicDSIClock(uint64_t *bitclk);
  virtual DisplayError SetConfigAttributes(uint32_t index, uint32_t width, uint32_t height);

  enum {
    kHWEventVSync,
    kHWEventBlank,
  };

  static const int kMaxStringLength = 1024;
  static const int kNumPhysicalDisplays = 2;
  // This indicates the number of fb devices created in the driver for all interfaces. Any addition
  // of new fb devices should be added here.
  static const int kFBNodeMax = 4;

  void DumpLayerCommit(const mdp_layer_commit &layer_commit);
  DisplayError SetFormat(const LayerBufferFormat &source, uint32_t *target);
  DisplayError SetStride(HWDeviceType device_type, LayerBufferFormat format,
                         uint32_t width, uint32_t *target);
  void SetBlending(const LayerBlending &source, mdss_mdp_blend_op *target);
  void SetRect(const LayerRect &source, mdp_rect *target);
  void SetMDPFlags(const Layer *layer, const bool &is_rotator_used,
                   bool is_cursor_pipe_used, uint32_t *mdp_flags);
  // Retrieves HW FrameBuffer Node Index
  int GetFBNodeIndex(HWDeviceType device_type);
  // Populates HWPanelInfo based on node index
  void PopulateHWPanelInfo();
  void PopulateBitClkRates();
  void GetHWPanelInfoByNode(int device_node, HWPanelInfo *panel_info);
  void GetHWPanelNameByNode(int device_node, HWPanelInfo *panel_info);
  void GetHWDisplayPortAndMode(int device_node, HWPanelInfo *panel_info);
  void GetSplitInfo(int device_node, HWPanelInfo *panel_info);
  void GetHWPanelMaxBrightnessFromNode(HWPanelInfo *panel_info);
  int ParseLine(const char *input, char *tokens[], const uint32_t max_token, uint32_t *count);
  int ParseLine(const char *input, const char *delim, char *tokens[],
                const uint32_t max_token, uint32_t *count);
  void ResetDisplayParams();
  void SetCSC(const ColorMetaData &color_metadata, mdp_color_space *color_space);
  void SetIGC(const LayerBuffer *layer_buffer, uint32_t index);

  bool EnableHotPlugDetection(int enable);
  ssize_t SysFsWrite(const char* file_node, const char* value, ssize_t length);
  bool IsFBNodeConnected(int fb_node);

  HWResourceInfo hw_resource_;
  HWPanelInfo hw_panel_info_;
  HWInfoInterface *hw_info_intf_;
  int fb_node_index_;
  const char *fb_path_;
  BufferSyncHandler *buffer_sync_handler_;
  int device_fd_ = -1;
  int stored_retire_fence = -1;
  HWDeviceType device_type_;
  mdp_layer_commit mdp_disp_commit_;
  mdp_input_layer mdp_in_layers_[kMaxSDELayers * 2];   // split panel (left + right)
  HWScale *hw_scale_ = NULL;
  mdp_overlay_pp_params pp_params_[kMaxSDELayers * 2];
  mdp_igc_lut_data_v1_7 igc_lut_data_[kMaxSDELayers * 2];
  mdp_output_layer mdp_out_layer_;
  const char *device_name_;
  bool synchronous_commit_;
  HWDisplayAttributes display_attributes_ = {};
  HWMixerAttributes mixer_attributes_ = {};
  std::vector<mdp_destination_scaler_data> mdp_dest_scalar_data_;
};

}  // namespace sdm

#endif  // __HW_DEVICE_H__

