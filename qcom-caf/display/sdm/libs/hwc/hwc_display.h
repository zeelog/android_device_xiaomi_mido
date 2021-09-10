/*
* Copyright (c) 2014 - 2017, The Linux Foundation. All rights reserved.
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

#ifndef __HWC_DISPLAY_H__
#define __HWC_DISPLAY_H__

#include <hardware/hwcomposer.h>
#include <core/core_interface.h>
#include <qdMetaData.h>
#include <QService.h>
#include <private/color_params.h>
#include <map>
#include <vector>
#include <string>

namespace sdm {

class BlitEngine;
class HWCToneMapper;

// Subclasses set this to their type. This has to be different from DisplayType.
// This is to avoid RTTI and dynamic_cast
enum DisplayClass {
  DISPLAY_CLASS_PRIMARY,
  DISPLAY_CLASS_EXTERNAL,
  DISPLAY_CLASS_VIRTUAL,
  DISPLAY_CLASS_NULL
};

class HWCColorMode {
 public:
  explicit HWCColorMode(DisplayInterface *display_intf) : display_intf_(display_intf) {}
  ~HWCColorMode() {}
  void Init();
  void DeInit() {}
  int SetColorMode(const std::string &color_mode);
  const std::vector<std::string> &GetColorModes();
  int SetColorTransform(uint32_t matrix_count, const float *matrix);

 private:
  static const uint32_t kColorTransformMatrixCount = 16;
  template <class T>
  void CopyColorTransformMatrix(const T *input_matrix, double *output_matrix) {
    for (uint32_t i = 0; i < kColorTransformMatrixCount; i++) {
      output_matrix[i] = static_cast<double>(input_matrix[i]);
    }
  }
  int PopulateColorModes();
  DisplayInterface *display_intf_ = NULL;
  std::vector<std::string> color_modes_ = {};
  std::string current_color_mode_ = {};
};

class HWCDisplay : public DisplayEventHandler {
 public:
  enum {
    SET_METADATA_DYN_REFRESH_RATE,
    SET_BINDER_DYN_REFRESH_RATE,
    SET_DISPLAY_MODE,
    SET_QDCM_SOLID_FILL_INFO,
    UNSET_QDCM_SOLID_FILL_INFO,
  };

  virtual ~HWCDisplay() { }
  virtual int Init();
  virtual int Deinit();
  virtual int Prepare(hwc_display_contents_1_t *content_list) = 0;
  virtual int Commit(hwc_display_contents_1_t *content_list) = 0;
  virtual int EventControl(int event, int enable);
  virtual int SetPowerMode(int mode);

  // Framebuffer configurations
  virtual int GetDisplayConfigs(uint32_t *configs, size_t *num_configs);
  virtual int GetDisplayAttributes(uint32_t config, const uint32_t *display_attributes,
                                   int32_t *values);
  virtual int GetActiveConfig();
  virtual int SetActiveConfig(int index);

  virtual void SetIdleTimeoutMs(uint32_t timeout_ms);
  virtual void SetFrameDumpConfig(uint32_t count, uint32_t bit_mask_layer_type);
  virtual DisplayError SetMaxMixerStages(uint32_t max_mixer_stages);
  virtual DisplayError ControlPartialUpdate(bool enable, uint32_t *pending) {
    return kErrorNotSupported;
  }
  virtual uint32_t GetLastPowerMode();
  virtual int SetFrameBufferResolution(uint32_t x_pixels, uint32_t y_pixels);
  virtual void GetFrameBufferResolution(uint32_t *x_pixels, uint32_t *y_pixels);
  virtual int SetDisplayStatus(uint32_t display_status);
  virtual int OnMinHdcpEncryptionLevelChange(uint32_t min_enc_level);
  virtual int Perform(uint32_t operation, ...);
  virtual int SetCursorPosition(int x, int y);
  virtual void SetSecureDisplay(bool secure_display_active, bool force_flush);
  virtual DisplayError SetMixerResolution(uint32_t width, uint32_t height);
  virtual DisplayError GetMixerResolution(uint32_t *width, uint32_t *height);
  virtual void GetPanelResolution(uint32_t *width, uint32_t *height);

  // Captures frame output in the buffer specified by output_buffer_info. The API is
  // non-blocking and the client is expected to check operation status later on.
  // Returns -1 if the input is invalid.
  virtual int FrameCaptureAsync(const BufferInfo& output_buffer_info, bool post_processed) {
    return -1;
  }
  // Returns the status of frame capture operation requested with FrameCaptureAsync().
  // -EAGAIN : No status obtain yet, call API again after another frame.
  // < 0 : Operation happened but failed.
  // 0 : Success.
  virtual int GetFrameCaptureStatus() { return -EAGAIN; }

  virtual DisplayError SetDetailEnhancerConfig(const DisplayDetailEnhancerData &de_data) {
    return kErrorNotSupported;
  }

  // Display Configurations
  virtual int SetActiveDisplayConfig(int config);
  virtual int GetActiveDisplayConfig(uint32_t *config);
  virtual int GetDisplayConfigCount(uint32_t *count);
  virtual int GetDisplayAttributesForConfig(int config,
                                            DisplayConfigVariableInfo *display_attributes);
  virtual int GetDisplayFixedConfig(DisplayConfigFixedInfo *fixed_info);

  int SetPanelBrightness(int level);
  int GetPanelBrightness(int *level);
  int CachePanelBrightness(int level);
  int ToggleScreenUpdates(bool enable);
  int ColorSVCRequestRoute(const PPDisplayAPIPayload &in_payload,
                           PPDisplayAPIPayload *out_payload,
                           PPPendingParams *pending_action);
  int GetVisibleDisplayRect(hwc_rect_t* rect);
  DisplayClass GetDisplayClass();
  int GetDisplayPort(DisplayPort *port);

 protected:
  enum DisplayStatus {
    kDisplayStatusOffline = 0,
    kDisplayStatusOnline,
    kDisplayStatusPause,
    kDisplayStatusResume,
  };

  // Dim layer flag set by SurfaceFlinger service.
  static const uint32_t kDimLayer = 0x80000000;

  // Maximum number of layers supported by display manager.
  static const uint32_t kMaxLayerCount = 32;

  HWCDisplay(CoreInterface *core_intf, hwc_procs_t const **hwc_procs, DisplayType type, int id,
             bool needs_blit, qService::QService *qservice, DisplayClass display_class);

  // DisplayEventHandler methods
  virtual DisplayError VSync(const DisplayEventVSync &vsync);
  virtual DisplayError Refresh();
  virtual DisplayError CECMessage(char *message);

  int AllocateLayerStack(hwc_display_contents_1_t *content_list);
  void FreeLayerStack();
  virtual int PrePrepareLayerStack(hwc_display_contents_1_t *content_list);
  virtual int PrepareLayerStack(hwc_display_contents_1_t *content_list);
  virtual int CommitLayerStack(hwc_display_contents_1_t *content_list);
  virtual int PostCommitLayerStack(hwc_display_contents_1_t *content_list);
  virtual void DumpOutputBuffer(const BufferInfo& buffer_info, void *base, int fence);
  virtual uint32_t RoundToStandardFPS(float fps);
  virtual uint32_t SanitizeRefreshRate(uint32_t req_refresh_rate);
  virtual void PrepareDynamicRefreshRate(Layer *layer);
  virtual DisplayError DisablePartialUpdateOneFrame() {
    return kErrorNotSupported;
  }
  inline void SetRect(const hwc_rect_t &source, LayerRect *target);
  inline void SetRect(const hwc_frect_t &source, LayerRect *target);
  inline void SetComposition(const int32_t &source, LayerComposition *target);
  inline void SetComposition(const LayerComposition &source, int32_t *target);
  inline void SetBlending(const int32_t &source, LayerBlending *target);
  int SetFormat(const int32_t &source, const int flags, LayerBufferFormat *target);
  void SetLayerS3DMode(const LayerBufferS3DFormat &source, uint32_t *target);
  LayerBufferFormat GetSDMFormat(const int32_t &source, const int flags);
  const char *GetDisplayString();
  void MarkLayersForGPUBypass(hwc_display_contents_1_t *content_list);
  virtual void ApplyScanAdjustment(hwc_rect_t *display_frame);
  DisplayError SetCSC(const MetaData_t *meta_data, ColorMetaData *color_metadata);
  DisplayError SetIGC(IGC_t source, LayerIGC *target);
  DisplayError SetMetaData(const private_handle_t *pvt_handle, Layer *layer);
  bool NeedsFrameBufferRefresh(hwc_display_contents_1_t *content_list);
  bool IsLayerUpdating(hwc_display_contents_1_t *content_list, const Layer *layer);
  bool IsNonIntegralSourceCrop(const hwc_frect_t &source);
  uint32_t GetUpdatingLayersCount(uint32_t app_layer_count);
  bool SingleVideoLayerUpdating(uint32_t app_layer_count);
  bool IsSurfaceUpdated(const std::vector<LayerRect> &dirty_regions);

  enum {
    INPUT_LAYER_DUMP,
    OUTPUT_LAYER_DUMP,
  };

  CoreInterface *core_intf_;
  hwc_procs_t const **hwc_procs_;
  DisplayType type_;
  int id_;
  bool needs_blit_ = false;
  DisplayInterface *display_intf_ = NULL;
  LayerStack layer_stack_;
  bool flush_on_error_ = false;
  bool flush_ = false;
  uint32_t dump_frame_count_ = 0;
  uint32_t dump_frame_index_ = 0;
  bool dump_input_layers_ = false;
  uint32_t last_power_mode_;
  bool swap_interval_zero_ = false;
  bool display_paused_ = false;
  uint32_t min_refresh_rate_ = 0;
  uint32_t max_refresh_rate_ = 0;
  uint32_t current_refresh_rate_ = 0;
  bool use_metadata_refresh_rate_ = false;
  uint32_t metadata_refresh_rate_ = 0;
  uint32_t force_refresh_rate_ = 0;
  bool boot_animation_completed_ = false;
  bool shutdown_pending_ = false;
  bool use_blit_comp_ = false;
  bool secure_display_active_ = false;
  uint32_t skip_prepare_cnt = 0;
  bool solid_fill_enable_ = false;
  bool disable_animation_ = false;
  uint32_t solid_fill_color_ = 0;
  LayerRect display_rect_;
  std::map<int, LayerBufferS3DFormat> s3d_format_hwc_to_sdm_;
  bool animating_ = false;
  HWCToneMapper *tone_mapper_ = NULL;
  HWCColorMode *color_mode_ = NULL;
  int disable_hdr_handling_ = 0;  // disables HDR handling.

 private:
  void DumpInputBuffers(hwc_display_contents_1_t *content_list);
  int PrepareLayerParams(hwc_layer_1_t *hwc_layer, Layer *layer);
  void CommitLayerParams(hwc_layer_1_t *hwc_layer, Layer *layer);
  BlitEngine *blit_engine_ = NULL;
  qService::QService *qservice_ = NULL;
  DisplayClass display_class_;
};

inline int HWCDisplay::Perform(uint32_t operation, ...) {
  return 0;
}

}  // namespace sdm

#endif  // __HWC_DISPLAY_H__

