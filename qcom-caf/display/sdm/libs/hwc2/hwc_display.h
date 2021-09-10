/*
 * Copyright (c) 2014-2018, 2020-2021 The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
 * Copyright 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __HWC_DISPLAY_H__
#define __HWC_DISPLAY_H__

#include <sys/stat.h>
#include <QService.h>
#include <core/core_interface.h>
#include <hardware/hwcomposer.h>
#include <private/color_params.h>
#include <qdMetaData.h>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "hwc_buffer_allocator.h"
#include "hwc_callbacks.h"
#include "hwc_layers.h"

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
  explicit HWCColorMode(DisplayInterface *display_intf);
  ~HWCColorMode() {}
  HWC2::Error Init();
  HWC2::Error DeInit();
  void Dump(std::ostringstream* os);
  uint32_t GetColorModeCount();
  HWC2::Error GetColorModes(uint32_t *out_num_modes, android_color_mode_t *out_modes);
  HWC2::Error SetColorMode(android_color_mode_t mode);
  HWC2::Error SetColorTransform(const float *matrix, android_color_transform_t hint);
  HWC2::Error RestoreColorTransform();

 private:
  static const uint32_t kColorTransformMatrixCount = 16;

  HWC2::Error HandleColorModeTransform(android_color_mode_t mode,
                                       android_color_transform_t hint, const double *matrix);
  void PopulateColorModes();
  void PopulateTransform(const android_color_mode_t &mode,
                         const std::string &color_mode, const std::string &color_transform);
  template <class T>
  void CopyColorTransformMatrix(const T *input_matrix, double *output_matrix) {
    for (uint32_t i = 0; i < kColorTransformMatrixCount; i++) {
      output_matrix[i] = static_cast<double>(input_matrix[i]);
    }
  }
  HWC2::Error ApplyDefaultColorMode();

  DisplayInterface *display_intf_ = NULL;
  android_color_mode_t current_color_mode_ = HAL_COLOR_MODE_NATIVE;
  android_color_transform_t current_color_transform_ = HAL_COLOR_TRANSFORM_IDENTITY;
  typedef std::map<android_color_transform_t, std::string> TransformMap;
  std::map<android_color_mode_t, TransformMap> color_mode_transform_map_ = {};
  double color_matrix_[kColorTransformMatrixCount] = { 1.0, 0.0, 0.0, 0.0, \
                                                       0.0, 1.0, 0.0, 0.0, \
                                                       0.0, 0.0, 1.0, 0.0, \
                                                       0.0, 0.0, 0.0, 1.0 };
};

class HWCDisplay : public DisplayEventHandler {
 public:
  enum DisplayStatus {
    kDisplayStatusInvalid = -1,
    kDisplayStatusOffline,
    kDisplayStatusOnline,
    kDisplayStatusPause,
    kDisplayStatusResume,
  };

  virtual ~HWCDisplay() {}
  virtual int Init();
  virtual int Deinit();

  // Framebuffer configurations
  virtual void SetIdleTimeoutMs(uint32_t timeout_ms);
  virtual void SetFrameDumpConfig(uint32_t count, uint32_t bit_mask_layer_type);
  virtual DisplayError SetMaxMixerStages(uint32_t max_mixer_stages);
  virtual DisplayError ControlPartialUpdate(bool enable, uint32_t *pending) {
    return kErrorNotSupported;
  }
  virtual HWC2::PowerMode GetLastPowerMode();
  virtual int SetFrameBufferResolution(uint32_t x_pixels, uint32_t y_pixels);
  virtual void GetFrameBufferResolution(uint32_t *x_pixels, uint32_t *y_pixels);
  virtual int SetDisplayStatus(DisplayStatus display_status);
  virtual int OnMinHdcpEncryptionLevelChange(uint32_t min_enc_level);
  virtual int Perform(uint32_t operation, ...);
  virtual void SetSecureDisplay(bool secure_display_active);
  virtual DisplayError SetMixerResolution(uint32_t width, uint32_t height);
  virtual DisplayError GetMixerResolution(uint32_t *width, uint32_t *height);
  virtual void GetPanelResolution(uint32_t *width, uint32_t *height);
  virtual std::string Dump();

  // Captures frame output in the buffer specified by output_buffer_info. The API is
  // non-blocking and the client is expected to check operation status later on.
  // Returns -1 if the input is invalid.
  virtual int FrameCaptureAsync(const BufferInfo &output_buffer_info, bool post_processed) {
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
  virtual int SetActiveDisplayConfig(uint32_t config);
  virtual int GetActiveDisplayConfig(uint32_t *config);
  virtual int GetDisplayConfigCount(uint32_t *count);
  virtual int GetDisplayAttributesForConfig(int config,
                                            DisplayConfigVariableInfo *display_attributes);
  virtual int SetState(bool connected) {
    return kErrorNotSupported;
  }
  virtual DisplayError SetStandByMode(bool enable) {
    return kErrorNotSupported;
  }
  int SetPanelBrightness(int level);
  void SetIsPrimaryPanel(bool is_primary) {
    is_primary_ = is_primary;
  }
  int GetPanelBrightness(int *level);
  int ToggleScreenUpdates(bool enable);
  int ColorSVCRequestRoute(const PPDisplayAPIPayload &in_payload, PPDisplayAPIPayload *out_payload,
                           PPPendingParams *pending_action);
  void SolidFillPrepare();
  void SolidFillCommit();
  DisplayClass GetDisplayClass();
  int GetVisibleDisplayRect(hwc_rect_t *rect);
  void BuildLayerStack(void);
  void BuildSolidFillStack(void);
  HWCLayer *GetHWCLayer(hwc2_layer_t layer);
  void ResetValidation() { validated_.reset(); }
  uint32_t GetGeometryChanges() { return geometry_changes_; }

  // HWC2 APIs
  virtual HWC2::Error AcceptDisplayChanges(void);
  virtual HWC2::Error GetActiveConfig(hwc2_config_t *out_config);
  virtual HWC2::Error SetActiveConfig(hwc2_config_t config);
  virtual HWC2::Error SetClientTarget(buffer_handle_t target, int32_t acquire_fence,
                                      int32_t dataspace, hwc_region_t damage);
  virtual HWC2::Error SetColorMode(android_color_mode_t mode) {
    return HWC2::Error::Unsupported;
  }
  virtual HWC2::Error RestoreColorTransform() {
    return HWC2::Error::Unsupported;
  }
  virtual HWC2::Error SetColorTransform(const float *matrix, android_color_transform_t hint) {
    return HWC2::Error::Unsupported;
  }
  virtual HWC2::Error HandleColorModeTransform(android_color_mode_t mode,
                                               android_color_transform_t hint,
                                               const double *matrix) {
    return HWC2::Error::Unsupported;
  }
  virtual HWC2::Error GetDisplayConfigs(uint32_t *out_num_configs, hwc2_config_t *out_configs);
  virtual HWC2::Error GetDisplayAttribute(hwc2_config_t config, HWC2::Attribute attribute,
                                          int32_t *out_value);
  virtual HWC2::Error GetClientTargetSupport(uint32_t width, uint32_t height, int32_t format,
                                             int32_t dataspace);
  virtual HWC2::Error GetColorModes(uint32_t *outNumModes, android_color_mode_t *outModes);
  virtual HWC2::Error GetChangedCompositionTypes(uint32_t *out_num_elements,
                                                 hwc2_layer_t *out_layers, int32_t *out_types);
  virtual HWC2::Error GetDisplayRequests(int32_t *out_display_requests, uint32_t *out_num_elements,
                                         hwc2_layer_t *out_layers, int32_t *out_layer_requests);
  virtual HWC2::Error GetDisplayName(uint32_t *out_size, char *out_name);
  virtual HWC2::Error GetDisplayType(int32_t *out_type);
  virtual HWC2::Error SetCursorPosition(hwc2_layer_t layer, int x, int y);
  virtual HWC2::Error SetVsyncEnabled(HWC2::Vsync enabled);
  virtual HWC2::Error SetPowerMode(HWC2::PowerMode mode);
  virtual HWC2::Error CreateLayer(hwc2_layer_t *out_layer_id);
  virtual HWC2::Error DestroyLayer(hwc2_layer_t layer_id);
  virtual HWC2::Error SetLayerZOrder(hwc2_layer_t layer_id, uint32_t z);
  virtual HWC2::Error Validate(uint32_t *out_num_types, uint32_t *out_num_requests) = 0;
  virtual HWC2::Error GetReleaseFences(uint32_t *out_num_elements, hwc2_layer_t *out_layers,
                                       int32_t *out_fences);
  virtual HWC2::Error Present(int32_t *out_retire_fence) = 0;
  virtual HWC2::Error GetHdrCapabilities(uint32_t *out_num_types, int32_t* out_types,
                                         float* out_max_luminance,
                                         float* out_max_average_luminance,
                                         float* out_min_luminance);
  virtual HWC2::Error SetDisplayAnimating(bool animating) {
    animating_ = animating;
    validated_ = false;
    return HWC2::Error::None;
  }
  virtual DisplayError SetDynamicDSIClock(uint64_t bitclk) {
    return kErrorNotSupported;
  }
  virtual DisplayError GetDynamicDSIClock(uint64_t *bitclk) {
    return kErrorNotSupported;
  }
  virtual DisplayError GetSupportedDSIClock(std::vector<uint64_t> *bitclk) {
    return kErrorNotSupported;
  }

 protected:
  // Maximum number of layers supported by display manager.
  static const uint32_t kMaxLayerCount = 32;

  HWCDisplay(CoreInterface *core_intf, HWCCallbacks *callbacks, DisplayType type, hwc2_display_t id,
             bool needs_blit, qService::QService *qservice, DisplayClass display_class,
             BufferAllocator *buffer_allocator);

  // DisplayEventHandler methods
  virtual DisplayError VSync(const DisplayEventVSync &vsync);
  virtual DisplayError Refresh();
  virtual DisplayError CECMessage(char *message);
  virtual DisplayError HandleEvent(DisplayEvent event);
  virtual void DumpOutputBuffer(const BufferInfo &buffer_info, void *base, int fence);
  virtual HWC2::Error PrepareLayerStack(uint32_t *out_num_types, uint32_t *out_num_requests);
  virtual HWC2::Error CommitLayerStack(void);
  virtual HWC2::Error PostCommitLayerStack(int32_t *out_retire_fence);
  virtual DisplayError DisablePartialUpdateOneFrame() {
    return kErrorNotSupported;
  }
  LayerBufferFormat GetSDMFormat(const int32_t &source, const int flags);
  const char *GetDisplayString();
  void MarkLayersForGPUBypass(void);
  void MarkLayersForClientComposition(void);
  virtual void ApplyScanAdjustment(hwc_rect_t *display_frame);
  uint32_t GetUpdatingLayersCount(void);
  bool IsSurfaceUpdated(const std::vector<LayerRect> &dirty_regions);
  bool IsLayerUpdating(const Layer *layer);
  uint32_t SanitizeRefreshRate(uint32_t req_refresh_rate);
  virtual void CloseAcquireFds();
  virtual void GetUnderScanConfig() { }

  enum {
    INPUT_LAYER_DUMP,
    OUTPUT_LAYER_DUMP,
  };

  static std::bitset<kDisplayMax> validated_;
  bool layer_stack_invalid_ = true;
  CoreInterface *core_intf_ = nullptr;
  HWCCallbacks *callbacks_  = nullptr;
  HWCBufferAllocator *buffer_allocator_ = NULL;
  DisplayType type_;
  hwc2_display_t id_;
  bool needs_blit_ = false;
  DisplayInterface *display_intf_ = NULL;
  LayerStack layer_stack_;
  HWCLayer *client_target_ = nullptr;                   // Also known as framebuffer target
  std::map<hwc2_layer_t, HWCLayer *> layer_map_;        // Look up by Id - TODO
  std::multiset<HWCLayer *, SortLayersByZ> layer_set_;  // Maintain a set sorted by Z
  std::map<hwc2_layer_t, HWC2::Composition> layer_changes_;
  std::map<hwc2_layer_t, HWC2::LayerRequest> layer_requests_;
  bool flush_on_error_ = false;
  bool flush_ = false;
  uint32_t dump_frame_count_ = 0;
  uint32_t dump_frame_index_ = 0;
  bool dump_input_layers_ = false;
  HWC2::PowerMode last_power_mode_;
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
  bool secure_display_transition_ = false;
  bool skip_prepare_ = false;
  bool solid_fill_enable_ = false;
  Layer *solid_fill_layer_ = NULL;
  LayerRect solid_fill_rect_ = {};
  uint32_t solid_fill_color_ = 0;
  LayerRect display_rect_;
  bool color_tranform_failed_ = false;
  HWCColorMode *color_mode_ = NULL;
  HWCToneMapper *tone_mapper_ = nullptr;
  uint32_t num_configs_ = 0;
  int disable_hdr_handling_ = 0;  // disables HDR handling.

 private:
  void DumpInputBuffers(void);
  bool CanSkipValidate();
  qService::QService *qservice_ = NULL;
  DisplayClass display_class_;
  uint32_t geometry_changes_ = GeometryChanges::kNone;
  bool skip_validate_ = false;
  bool animating_ = false;
  bool fbt_valid_ = false;
  bool is_primary_ = false;
  uint32_t min_enc_level_ = UINT32_MAX;
};

inline int HWCDisplay::Perform(uint32_t operation, ...) {
  return 0;
}

}  // namespace sdm

#endif  // __HWC_DISPLAY_H__
