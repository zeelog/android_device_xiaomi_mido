/*
* Copyright (c) 2017, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above
*     copyright notice, this list of conditions and the following
*     disclaimer in the documentation and/or other materials provided
*     with the distribution.
*   * Neither the name of The Linux Foundation nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
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

#ifndef __DRM_INTERFACE_H__
#define __DRM_INTERFACE_H__

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "xf86drm.h"
#include "xf86drmMode.h"

namespace sde_drm {
/*
 * Drm Atomic Operation Codes
 */
enum struct DRMOps {
  /*
   * Op: Sets plane source crop
   * Arg: uint32_t - Plane ID
   *      DRMRect  - Source Rectangle
   */
  PLANE_SET_SRC_RECT,
  /*
   * Op: Sets plane destination rect
   * Arg: uint32_t - Plane ID
   *      DRMRect - Dst Rectangle
   */
  PLANE_SET_DST_RECT,
  /*
   * Op: Sets plane zorder
   * Arg: uint32_t - Plane ID
   *      uint32_t - zorder
   */
  PLANE_SET_ZORDER,
  /*
   * Op: Sets plane rotation flags
   * Arg: uint32_t - Plane ID
   *      uint32_t - bit mask of rotation flags (See drm_mode.h for enums)
   */
  PLANE_SET_ROTATION,
  /*
   * Op: Sets plane alpha
   * Arg: uint32_t - Plane ID
   *      uint32_t - alpha value
   */
  PLANE_SET_ALPHA,
  /*
   * Op: Sets the blend type
   * Arg: uint32_t - Plane ID
   *      uint32_t - blend type (see DRMBlendType)
   */
  PLANE_SET_BLEND_TYPE,
  /*
   * Op: Sets horizontal decimation
   * Arg: uint32_t - Plane ID
   *      uint32_t - decimation factor
   */
  PLANE_SET_H_DECIMATION,
  /*
   * Op: Sets vertical decimation
   * Arg: uint32_t - Plane ID
   *      uint32_t - decimation factor
   */
  PLANE_SET_V_DECIMATION,
  /*
   * Op: Sets source config flags
   * Arg: uint32_t - Plane ID
   *      uint32_t - flags to enable or disable a specific op. E.g. deinterlacing
   */
  PLANE_SET_SRC_CONFIG,
  /*
   * Op: Sets frame buffer ID for plane. Set together with CRTC.
   * Arg: uint32_t - Plane ID
   *      uint32_t - Framebuffer ID
   */
  PLANE_SET_FB_ID,
  /*
   * Op: Sets the crtc for this plane. Set together with FB_ID.
   * Arg: uint32_t - Plane ID
   *      uint32_t - CRTC ID
   */
  PLANE_SET_CRTC,
  /*
   * Op: Sets acquire fence for this plane's buffer. Set together with FB_ID, CRTC.
   * Arg: uint32_t - Plane ID
   *      uint32_t - Input fence
   */
  PLANE_SET_INPUT_FENCE,
  /*
   * Op: Sets scaler config on this plane.
   * Arg: uint32_t - Plane ID
   *      uint64_t - Address of the scaler config object (version based)
   */
  PLANE_SET_SCALER_CONFIG,
  /*
   * Op: Activate or deactivate a CRTC
   * Arg: uint32_t - CRTC ID
   *      uint32_t - 1 to enable, 0 to disable
   */
  CRTC_SET_ACTIVE,
  /*
   * Op: Sets display mode
   * Arg: uint32_t - CRTC ID
   *      drmModeModeInfo* - Pointer to display mode
   */
  CRTC_SET_MODE,
  /*
   * Op: Sets an offset indicating when a release fence should be signalled.
   * Arg: uint32_t - offset
   *      0: non-speculative, default
   *      1: speculative
   */
  CRTC_SET_OUTPUT_FENCE_OFFSET,
  /*
   * Op: Returns release fence for this frame. Should be called after Commit() on
   * DRMAtomicReqInterface.
   * Arg: uint32_t - CRTC ID
   *      int * - Pointer to an integer that will hold the returned fence
   */
  CRTC_GET_RELEASE_FENCE,
  /*
   * Op: Sets PP feature
   * Arg: uint32_t - CRTC ID
   *      DRMPPFeatureInfo * - PP feature data pointer
   */
  CRTC_SET_POST_PROC,
  /*
   * Op: Returns retire fence for this commit. Should be called after Commit() on
   * DRMAtomicReqInterface.
   * Arg: uint32_t - Connector ID
   *      int * - Pointer to an integer that will hold the returned fence
   */
  CONNECTOR_GET_RETIRE_FENCE,
  /*
   * Op: Sets writeback connector destination rect
   * Arg: uint32_t - Connector ID
   *      DRMRect - Dst Rectangle
   */
  CONNECTOR_SET_OUTPUT_RECT,
  /*
   * Op: Sets frame buffer ID for writeback connector.
   * Arg: uint32_t - Connector ID
   *      uint32_t - Framebuffer ID
   */
  CONNECTOR_SET_OUTPUT_FB_ID,
};

enum struct DRMRotation {
  FLIP_H = 0x1,
  FLIP_V = 0x2,
  ROT_90 = 0x4,
};

enum struct DRMBlendType {
  UNDEFINED = 0,
  OPAQUE = 1,
  PREMULTIPLIED = 2,
  COVERAGE = 3,
};

enum struct DRMSrcConfig {
  DEINTERLACE = 0,
};

/* Display type to identify a suitable connector */
enum struct DRMDisplayType {
  PERIPHERAL,
  TV,
  VIRTUAL,
};

struct DRMRect {
  uint32_t left;    // Left-most pixel coordinate.
  uint32_t top;     // Top-most pixel coordinate.
  uint32_t right;   // Right-most pixel coordinate.
  uint32_t bottom;  // Bottom-most pixel coordinate.
};

//------------------------------------------------------------------------
// DRM Info Query Types
//------------------------------------------------------------------------

enum struct QSEEDVersion {
  V1,
  V2,
  V3,
};

enum struct SmartDMARevision {
  V1,
  V2,
};

/* Per CRTC Resource Info*/
struct DRMCrtcInfo {
  bool has_src_split;
  uint32_t max_blend_stages;
  QSEEDVersion qseed_version;
  SmartDMARevision smart_dma_rev;
};

enum struct DRMPlaneType {
  // Has CSC and scaling capability
  VIG = 0,
  // Has scaling capability but no CSC
  RGB,
  // No scaling support
  DMA,
  // Supports a small dimension and doesn't use a CRTC stage
  CURSOR,
  MAX,
};

struct DRMPlaneTypeInfo {
  DRMPlaneType type;
  uint32_t master_plane_id;
  // FourCC format enum and modifier
  std::vector<std::pair<uint32_t, uint64_t>> formats_supported;
  uint32_t max_linewidth;
  uint32_t max_upscale;
  uint32_t max_downscale;
  uint32_t max_horizontal_deci;
  uint32_t max_vertical_deci;
};

// All DRM Planes as map<Plane_id , plane_type_info> listed from highest to lowest priority
typedef std::vector<std::pair<uint32_t, DRMPlaneTypeInfo>>  DRMPlanesInfo;

enum struct DRMTopology {
  UNKNOWN,  // To be compat with driver defs in sde_kms.h
  SINGLE_LM,
  DUAL_LM,
  PPSPLIT,
  DUAL_LM_MERGE,
};

enum struct DRMPanelMode {
  VIDEO,
  COMMAND,
};

/* Per Connector Info*/
struct DRMConnectorInfo {
  uint32_t mmWidth;
  uint32_t mmHeight;
  uint32_t type;
  uint32_t num_modes;
  drmModeModeInfo *modes;
  DRMTopology topology;
  std::string panel_name;
  DRMPanelMode panel_mode;
  bool is_primary;
  // Valid only if DRMPanelMode is VIDEO
  bool dynamic_fps;
  // FourCC format enum and modifier
  std::vector<std::pair<uint32_t, uint64_t>> formats_supported;
  // Valid only if type is DRM_MODE_CONNECTOR_VIRTUAL
  uint32_t max_linewidth;
};

/* Identifier token for a display */
struct DRMDisplayToken {
  uint32_t conn_id;
  uint32_t crtc_id;
};

enum DRMPPFeatureID {
  kFeaturePcc,
  kFeatureIgc,
  kFeaturePgc,
  kFeatureMixerGc,
  kFeaturePaV2,
  kFeatureDither,
  kFeatureGamut,
  kFeaturePADither,
  kPPFeaturesMax,
};

enum DRMPPPropType {
  kPropEnum,
  kPropRange,
  kPropBlob,
  kPropTypeMax,
};

struct DRMPPFeatureInfo {
  DRMPPFeatureID id;
  DRMPPPropType type;
  uint32_t version;
  uint32_t payload_size;
  void *payload;
};

struct DRMScalerLUTInfo {
  uint32_t dir_lut_size = 0;
  uint32_t cir_lut_size = 0;
  uint32_t sep_lut_size = 0;
  uint64_t dir_lut = 0;
  uint64_t cir_lut = 0;
  uint64_t sep_lut = 0;
};

/* DRM Atomic Request Property Set.
 *
 * Helper class to create and populate atomic properties of DRM components
 * when rendered in DRM atomic mode */
class DRMAtomicReqInterface {
 public:
  virtual ~DRMAtomicReqInterface() {}
  /* Perform request operation.
   *
   * [input]: opcode: operation code from DRMOps list.
   *          var_arg: arguments for DRMOps's can differ in number and
   *          data type. Refer above DRMOps to details.
   * [return]: Error code if the API fails, 0 on success.
   */
  virtual int Perform(DRMOps opcode, ...) = 0;

  /*
   * Commit the params set via Perform(). Also resets the properties after commit. Needs to be
   * called every frame.
   * [input]: synchronous: Determines if the call should block until a h/w flip
   * [return]: Error code if the API fails, 0 on success.
   */
  virtual int Commit(bool synchronous) = 0;
  /*
   * Validate the params set via Perform().
   * [return]: Error code if the API fails, 0 on success.
   */
  virtual int Validate() = 0;
};

class DRMManagerInterface;

/* Populates a singleton instance of DRMManager */
typedef int (*GetDRMManager)(int fd, DRMManagerInterface **intf);

/* Destroy DRMManager instance */
typedef int (*DestroyDRMManager)();

/*
 * DRM Manager Interface - Any class which plans to implement helper function for vendor
 * specific DRM driver implementation must implement the below interface routines to work
 * with SDM.
 */

class DRMManagerInterface {
 public:
  virtual ~DRMManagerInterface() {}

  /*
   * Since SDM completely manages the planes. GetPlanesInfo will provide all
   * the plane information.
   * [output]: DRMPlanesInfo: Resource Info for planes.
   */
  virtual void GetPlanesInfo(DRMPlanesInfo *info) = 0;

  /*
   * Will provide all the information of a selected crtc.
   * [input]: Use crtc id 0 to obtain system wide info
   * [output]: DRMCrtcInfo: Resource Info for the given CRTC id.
   */
  virtual void GetCrtcInfo(uint32_t crtc_id, DRMCrtcInfo *info) = 0;

  /*
   * Will provide all the information of a selected connector.
   * [output]: DRMConnectorInfo: Resource Info for the given connector id
   */
  virtual void GetConnectorInfo(uint32_t conn_id, DRMConnectorInfo *info) = 0;

  /*
   * Will query post propcessing feature info of a CRTC.
   * [output]: DRMPPFeatureInfo: CRTC post processing feature info
   */
   virtual void GetCrtcPPInfo(uint32_t crtc_id, DRMPPFeatureInfo &info) = 0;
  /*
   * Register a logical display to receive a token.
   * Each display pipeline in DRM is identified by its CRTC and Connector(s).
   * On display connect(bootup or hotplug), clients should invoke this interface to
   * establish the pipeline for the display and should get a DisplayToken
   * populated with crtc and connnector(s) id's. Here onwards, Client should
   * use this token to represent the display for any Perform operations if
   * needed.
   *
   * [input]: disp_type - Peripheral / TV / Virtual
   * [output]: DRMDisplayToken - CRTC and Connector id's for the display
   * [return]: 0 on success, a negative error value otherwise
   */
  virtual int RegisterDisplay(DRMDisplayType disp_type, DRMDisplayToken *tok) = 0;

  /* Client should invoke this interface on display disconnect.
   * [input]: DRMDisplayToken - identifier for the display.
   */
  virtual void UnregisterDisplay(const DRMDisplayToken &token) = 0;

  /*
   * Creates and returns an instance of DRMAtomicReqInterface corresponding to a display token
   * returned as part of RegisterDisplay API. Needs to be called per display.
   * [input]: DRMDisplayToken that identifies a display pipeline
   * [output]: Pointer to an instance of DRMAtomicReqInterface.
   * [return]: Error code if the API fails, 0 on success.
   */
  virtual int CreateAtomicReq(const DRMDisplayToken &token, DRMAtomicReqInterface **intf) = 0;

  /*
   * Destroys the instance of DRMAtomicReqInterface
   * [input]: Pointer to a DRMAtomicReqInterface
   * [return]: Error code if the API fails, 0 on success.
   */
  virtual int DestroyAtomicReq(DRMAtomicReqInterface *intf) = 0;
  /*
   * Sets the global scaler LUT
   * [input]: LUT Info
   * [return]: Error code if the API fails, 0 on success.
   */
  virtual int SetScalerLUT(const DRMScalerLUTInfo &lut_info) = 0;
};

}  // namespace sde_drm
#endif  // __DRM_INTERFACE_H__
