/*
* Copyright (c) 2016-2018, 2020 The Linux Foundation. All rights reserved.
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

#ifndef __COLOR_METADATA_H__
#define __COLOR_METADATA_H__

#ifdef __cplusplus
extern "C" {
#else
#include <stdbool.h>
#endif
#include <stdint.h>

typedef enum ColorRange {
  Range_Limited   = 0,
  Range_Full      = 1,
  Range_Extended  = 2,
  Range_Max     = 0xff,
} ColorRange;

// The following values matches the HEVC spec
typedef enum ColorPrimaries {
  // Unused = 0;
  ColorPrimaries_BT709_5     = 1,  // ITU-R BT.709-5 or equivalent
  /* Unspecified = 2, Reserved = 3*/
  ColorPrimaries_BT470_6M    = 4,  // ITU-R BT.470-6 System M or equivalent
  ColorPrimaries_BT601_6_625 = 5,  // ITU-R BT.601-6 625 or equivalent
  ColorPrimaries_BT601_6_525 = 6,  // ITU-R BT.601-6 525 or equivalent
  ColorPrimaries_SMPTE_240M  = 7,  // SMPTE_240M
  ColorPrimaries_GenericFilm = 8,  // Generic Film
  ColorPrimaries_BT2020      = 9,  // ITU-R BT.2020 or equivalent
  ColorPrimaries_SMPTE_ST428 = 10,  // SMPTE_240M
  ColorPrimaries_AdobeRGB    = 11,
  ColorPrimaries_DCIP3       = 12,
  ColorPrimaries_EBU3213     = 22,
  ColorPrimaries_Max         = 0xff,
} ColorPrimaries;

typedef enum GammaTransfer {
  // Unused = 0;
  Transfer_sRGB            = 1,  // ITR-BT.709-5
  /* Unspecified = 2, Reserved = 3 */
  Transfer_Gamma2_2        = 4,
  Transfer_Gamma2_8        = 5,
  Transfer_SMPTE_170M      = 6,  // BT.601-6 525 or 625
  Transfer_SMPTE_240M      = 7,  // SMPTE_240M
  Transfer_Linear          = 8,
  Transfer_Log             = 9,
  Transfer_Log_Sqrt        = 10,
  Transfer_XvYCC           = 11,  // IEC 61966-2-4
  Transfer_BT1361          = 12,  // Rec.ITU-R BT.1361 extended gamut
  Transfer_sYCC            = 13,  // IEC 61966-2-1 sRGB or sYCC
  Transfer_BT2020_2_1      = 14,  // Rec. ITU-R BT.2020-2 (same as the values 1, 6, and 15)
  Transfer_BT2020_2_2      = 15,  // Rec. ITU-R BT.2020-2 (same as the values 1, 6, and 14)
  Transfer_SMPTE_ST2084    = 16,  // 2084
  // transfers unlikely to be required by Android
  Transfer_ST_428          = 17,  // SMPTE ST 428-1
  Transfer_HLG             = 18,  // ARIB STD-B67
  Transfer_Max             = 0xff,
} GammaTransfer;

typedef enum MatrixCoEfficients {
  MatrixCoEff_Identity           = 0,
  MatrixCoEff_BT709_5            = 1,
  /* Unspecified = 2, Reserved = 3 */
  MatrixCoeff_FCC_73_682         = 4,
  MatrixCoEff_BT601_6_625        = 5,
  MatrixCoEff_BT601_6_525        = 6,
  MatrixCoEff_SMPTE240M          = 7,  // used with 601_525_Unadjusted
  MatrixCoEff_YCgCo              = 8,
  MatrixCoEff_BT2020             = 9,
  MatrixCoEff_BT2020Constant     = 10,
  MatrixCoEff_BT601_6_Unadjusted = 11,  // Used with BT601_625(KR=0.222, KB=0.071)
  MatrixCoEff_DCIP3              = 12,
  MatrixCoEff_Chroma_NonConstant = 13,
  MatrixCoEff_Max                = 0xff,
} MatrixCoEfficients;

typedef struct Primaries {
  uint32_t rgbPrimaries[3][2];  // unit 1/50000;
  uint32_t whitePoint[2];  // unit 1/50000;
} Primaries;

typedef struct MasteringDisplay {
  bool      colorVolumeSEIEnabled;
  Primaries primaries;
  uint32_t  maxDisplayLuminance;  // unit: cd/m^2.
  uint32_t  minDisplayLuminance;  // unit: 1/10000 cd/m^2.
} MasteringDisplay;

typedef struct ContentLightLevel {
  bool     lightLevelSEIEnabled;
  uint32_t maxContentLightLevel;  // unit: cd/m^2.
  uint32_t minPicAverageLightLevel;  // unit: cd/m^2, will be DEPRECATED, use below
  uint32_t maxPicAverageLightLevel;  // unit: cd/m^2, its same as maxFrameAvgLightLevel(CTA-861-G)
} ContentLightLevel;

typedef struct ColorRemappingInfo {
  bool               criEnabled;
  uint32_t           crId;
  uint32_t           crCancelFlag;
  uint32_t           crPersistenceFlag;
  uint32_t           crVideoSignalInfoPresentFlag;
  uint32_t           crRange;
  ColorPrimaries     crPrimaries;
  GammaTransfer      crTransferFunction;
  MatrixCoEfficients crMatrixCoefficients;
  uint32_t           crInputBitDepth;
  uint32_t           crOutputBitDepth;
  uint32_t           crPreLutNumValMinusOne[3];
  uint32_t           crPreLutCodedValue[3*33];
  uint32_t           crPreLutTargetValue[3*33];
  uint32_t           crMatrixPresentFlag;
  uint32_t           crLog2MatrixDenom;
  int32_t            crCoefficients[3*3];
  uint32_t           crPostLutNumValMinusOne[3];
  uint32_t           crPostLutCodedValue[3*33];
  uint32_t           crPostLutTargetValue[3*33];
} ColorRemappingInfo;

#define HDR_DYNAMIC_META_DATA_SZ 1024
typedef struct ColorMetaData {
  // Default values based on sRGB, needs to be overridden in gralloc
  // based on the format and size.
  ColorPrimaries     colorPrimaries;
  ColorRange         range;
  GammaTransfer      transfer;
  MatrixCoEfficients matrixCoefficients;

  MasteringDisplay   masteringDisplayInfo;
  ContentLightLevel  contentLightLevel;
  ColorRemappingInfo cRI;

  // Dynamic meta data elements
  bool dynamicMetaDataValid;
  uint32_t dynamicMetaDataLen;
  uint8_t dynamicMetaDataPayload[HDR_DYNAMIC_META_DATA_SZ];
} ColorMetaData;

typedef struct Color10Bit {
  uint32_t R: 10;
  uint32_t G: 10;
  uint32_t B: 10;
  uint32_t A: 2;
} Color10Bit;

typedef struct Lut3d {
  uint16_t dim;  // dimension of each side of LUT cube (ex: 13, 17)in lutEntries
  uint16_t gridSize;  // number of elements in the gridEntries
  /* Matrix ordering convension
  for (b = 0; b < dim; b++) {
    for (g = 0; g < dim; g++) {
      for (r = 0; r < dim; r++) {
        read/write [mR mG mB] associated w/ 3DLUT[r][g][b] to/from file
      }
    }
  } */
  Color10Bit *lutEntries;
  bool validLutEntries;  // Indicates if entries are valid and can be used.
  /*
   The grid is a 1D LUT for each of the R,G,B channels that can be
   used to apply an independent nonlinear transformation to each
   channel before it is used as a coordinate for addressing
   the uniform 3D LUT.  This effectively creates a non-uniformly
   sampled 3D LUT.  This is useful for having independent control
   of the sampling grid density along each dimension for greater
   precision in spite of having a relatively small number of samples.i
  */
  Color10Bit *gridEntries;
  bool validGridEntries;  // Indicates if entries are valid and can be used.
} Lut3d;

#ifdef __cplusplus
}
#endif

#endif  // __COLOR_METADATA_H__
