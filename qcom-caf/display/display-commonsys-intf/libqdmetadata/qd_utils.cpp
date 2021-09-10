/*
 * Copyright (c) 2013, 2018, 2020 The Linux Foundation. All rights reserved.

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

#include <unistd.h>
#include <gralloc_priv.h>
#include "qd_utils.h"

static const int kFBNodeMax = 4;
namespace qdutils {

static int parseLine(char *input, char *tokens[], const uint32_t maxToken, uint32_t *count) {
    char *tmpToken = NULL;
    char *tmpPtr;
    uint32_t index = 0;
    const char *delim = ", =\n";
    if (!input) {
      return -1;
    }
    tmpToken = strtok_r(input, delim, &tmpPtr);
    while (tmpToken && index < maxToken) {
      tokens[index++] = tmpToken;
      tmpToken = strtok_r(NULL, delim, &tmpPtr);
    }
    *count = index;

    return 0;
}

static int getExternalNode(const char *type) {
    FILE *displayDeviceFP = NULL;
    char fbType[MAX_FRAME_BUFFER_NAME_SIZE];
    char msmFbTypePath[MAX_FRAME_BUFFER_NAME_SIZE];
    int j = 0;

    for(j = 0; j < kFBNodeMax; j++) {
        snprintf (msmFbTypePath, sizeof(msmFbTypePath),
                  "/sys/devices/virtual/graphics/fb%d/msm_fb_type", j);
        displayDeviceFP = fopen(msmFbTypePath, "r");
        if(displayDeviceFP) {
            fread(fbType, sizeof(char), MAX_FRAME_BUFFER_NAME_SIZE,
                    displayDeviceFP);
            if(strncmp(fbType, type, strlen(type)) == 0) {
                ALOGD("%s: %s is at fb%d", __func__, type, j);
                fclose(displayDeviceFP);
                break;
            }
            fclose(displayDeviceFP);
        } else {
            ALOGE("%s: Failed to open fb node %s", __func__, msmFbTypePath);
        }
    }

    if (j < kFBNodeMax)
        return j;
    else
        ALOGE("%s: Failed to find %s node", __func__, type);

    return -1;
}

int getHDMINode(void) {
    return getExternalNode("dtv panel");
}

int getEdidRawData(char *buffer)
{
    int size;
    int edidFile;
    char msmFbTypePath[MAX_FRAME_BUFFER_NAME_SIZE];
    int node_id = getHDMINode();

    if (node_id < 0) {
        ALOGE("%s no HDMI node found", __func__);
        return 0;
    }

    snprintf(msmFbTypePath, sizeof(msmFbTypePath),
                 "/sys/devices/virtual/graphics/fb%d/edid_raw_data", node_id);

    edidFile = open(msmFbTypePath, O_RDONLY, 0);

    if (edidFile < 0) {
        ALOGE("%s no edid raw data found %s", __func__,msmFbTypePath);
        return 0;
    }

    size = (int)read(edidFile, (char*)buffer, EDID_RAW_DATA_SIZE);
    close(edidFile);
    return size;
}

bool isDPConnected() {
    char connectPath[MAX_FRAME_BUFFER_NAME_SIZE];
    FILE *connectFile = NULL;
    size_t len = MAX_STRING_LENGTH;
    char stringBuffer[MAX_STRING_LENGTH];
    char *line = stringBuffer;

    int nodeId = getExternalNode("dp panel");
    if (nodeId < 0) {
        ALOGE("%s no DP node found", __func__);
        return false;
    }

    snprintf(connectPath, sizeof(connectPath),
             "/sys/devices/virtual/graphics/fb%d/connected", nodeId);

    connectFile = fopen(connectPath, "rb");
    if (!connectFile) {
        ALOGW("Failed to open connect node for device node %s", connectPath);
        return false;
    }

    if (getline(&line, &len, connectFile) < 0) {
        fclose(connectFile);
        return false;
    }

    fclose(connectFile);

    return atoi(line);
}

int getDPTestConfig(uint32_t *panelBpp, uint32_t *patternType) {
    if (!panelBpp || !patternType) {
        return -1;
    }

    char configPath[MAX_FRAME_BUFFER_NAME_SIZE];
    FILE *configFile = NULL;
    uint32_t tokenCount = 0;
    const uint32_t maxCount = 10;
    char *tokens[maxCount] = { NULL };
    size_t len = MAX_STRING_LENGTH;
    char stringBuffer[MAX_STRING_LENGTH];
    char *line = stringBuffer;

    int nodeId = getExternalNode("dp panel");
    if (nodeId < 0) {
        ALOGE("%s no DP node found", __func__);
        return -EINVAL;
    }

    snprintf(configPath, sizeof(configPath),
             "/sys/devices/virtual/graphics/fb%d/config", nodeId);

    configFile = fopen(configPath, "rb");
    if (!configFile) {
        ALOGW("Failed to open config node for device node %s", configPath);
        return -EINVAL;
    }

    while (getline(&line, &len, configFile) != -1) {
        if (!parseLine(line, tokens, maxCount, &tokenCount)) {
            if (tokens[0] != NULL) {
              if (!strncmp(tokens[0], "bpp", strlen("bpp"))) {
                *panelBpp = static_cast<uint32_t>(atoi(tokens[1]));
              } else  if (!strncmp(tokens[0], "pattern", strlen("pattern"))) {
                *patternType = static_cast<uint32_t>(atoi(tokens[1]));
              }
            }
        }
    }

    fclose(configFile);

    return 0;
}

DriverType getDriverType() {
    const char *fb_caps = "/sys/devices/virtual/graphics/fb0/mdp/caps";
    // 0 - File exists
    return access(fb_caps, F_OK) ? DriverType::DRM : DriverType::FB;
}

const char *GetHALPixelFormatString(int format) {
  switch (format) {
  case HAL_PIXEL_FORMAT_RGBA_8888:
    return "RGBA_8888";
  case HAL_PIXEL_FORMAT_RGBX_8888:
    return "RGBX_8888";
  case HAL_PIXEL_FORMAT_RGB_888:
    return "RGB_888";
  case HAL_PIXEL_FORMAT_RGB_565:
    return "RGB_565";
  case HAL_PIXEL_FORMAT_BGR_565:
    return "BGR_565";
  case HAL_PIXEL_FORMAT_BGRA_8888:
    return "BGRA_8888";
  case HAL_PIXEL_FORMAT_RGBA_5551:
    return "RGBA_5551";
  case HAL_PIXEL_FORMAT_RGBA_4444:
    return "RGBA_4444";
  case HAL_PIXEL_FORMAT_YV12:
    return "YV12";
  case HAL_PIXEL_FORMAT_YCbCr_422_SP:
    return "YCbCr_422_SP_NV16";
  case HAL_PIXEL_FORMAT_YCrCb_420_SP:
    return "YCrCb_420_SP_NV21";
  case HAL_PIXEL_FORMAT_YCbCr_422_I:
    return "YCbCr_422_I_YUY2";
  case HAL_PIXEL_FORMAT_YCrCb_422_I:
    return "YCrCb_422_I_YVYU";
  case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:
    return "NV12_ENCODEABLE";
  case HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED:
    return "YCbCr_420_SP_TILED_TILE_4x2";
  case HAL_PIXEL_FORMAT_YCbCr_420_SP:
    return "YCbCr_420_SP";
  case HAL_PIXEL_FORMAT_YCrCb_420_SP_ADRENO:
    return "YCrCb_420_SP_ADRENO";
  case HAL_PIXEL_FORMAT_YCrCb_422_SP:
    return "YCrCb_422_SP";
  case HAL_PIXEL_FORMAT_R_8:
    return "R_8";
  case HAL_PIXEL_FORMAT_RG_88:
    return "RG_88";
  case HAL_PIXEL_FORMAT_INTERLACE:
    return "INTERLACE";
  case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS:
    return "YCbCr_420_SP_VENUS";
  case HAL_PIXEL_FORMAT_YCrCb_420_SP_VENUS:
    return "YCrCb_420_SP_VENUS";
  case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS_UBWC:
    return "YCbCr_420_SP_VENUS_UBWC";
  case HAL_PIXEL_FORMAT_RGBA_1010102:
    return "RGBA_1010102";
  case HAL_PIXEL_FORMAT_ARGB_2101010:
    return "ARGB_2101010";
  case HAL_PIXEL_FORMAT_RGBX_1010102:
    return "RGBX_1010102";
  case HAL_PIXEL_FORMAT_XRGB_2101010:
    return "XRGB_2101010";
  case HAL_PIXEL_FORMAT_BGRA_1010102:
    return "BGRA_1010102";
  case HAL_PIXEL_FORMAT_ABGR_2101010:
    return "ABGR_2101010";
  case HAL_PIXEL_FORMAT_BGRX_1010102:
    return "BGRX_1010102";
  case HAL_PIXEL_FORMAT_XBGR_2101010:
    return "XBGR_2101010";
  case HAL_PIXEL_FORMAT_YCbCr_420_P010:
    return "YCbCr_420_P010";
  case HAL_PIXEL_FORMAT_YCbCr_420_TP10_UBWC:
    return "YCbCr_420_TP10_UBWC";
  case HAL_PIXEL_FORMAT_YCbCr_420_P010_VENUS:
    return "YCbCr_420_P010_VENUS";
  default:
    return "Unknown_format";
  }
}

}; //namespace qdutils
