/*
* Copyright (c) 2015 - 2017, The Linux Foundation. All rights reserved.
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

// Adreno Pixel Formats
typedef enum {

    ADRENO_PIXELFORMAT_UNKNOWN       = 0,
    ADRENO_PIXELFORMAT_R10G10B10A2_UNORM = 24,  // Vertex, Normalized GL_UNSIGNED_INT_10_10_10_2_OES
    ADRENO_PIXELFORMAT_R8G8B8A8      = 28,
    ADRENO_PIXELFORMAT_R8G8B8A8_SRGB = 29,
    ADRENO_PIXELFORMAT_B5G6R5        = 85,
    ADRENO_PIXELFORMAT_B5G5R5A1      = 86,
    ADRENO_PIXELFORMAT_B8G8R8A8      = 90,
    ADRENO_PIXELFORMAT_B8G8R8A8_SRGB = 91,
    ADRENO_PIXELFORMAT_B8G8R8X8_SRGB = 93,
    ADRENO_PIXELFORMAT_NV12          = 103,
    ADRENO_PIXELFORMAT_P010          = 104,
    ADRENO_PIXELFORMAT_YUY2          = 107,
    ADRENO_PIXELFORMAT_B4G4R4A4      = 115,
    ADRENO_PIXELFORMAT_NV12_EXT      = 506,  // NV12 with non-std alignment and offsets
    ADRENO_PIXELFORMAT_R8G8B8X8      = 507,  // GL_RGB8 (Internal)
    ADRENO_PIXELFORMAT_R8G8B8        = 508,  // GL_RGB8
    ADRENO_PIXELFORMAT_A1B5G5R5      = 519,  // GL_RGB5_A1
    ADRENO_PIXELFORMAT_R8G8B8X8_SRGB = 520,  // GL_SRGB8
    ADRENO_PIXELFORMAT_R8G8B8_SRGB   = 521,  // GL_SRGB8
    ADRENO_PIXELFORMAT_A2B10G10R10_UNORM = 532,
                                             // Vertex, Normalized GL_UNSIGNED_INT_10_10_10_2_OES
    ADRENO_PIXELFORMAT_R10G10B10X2_UNORM = 537,
                                             // Vertex, Normalized GL_UNSIGNED_INT_10_10_10_2_OES
    ADRENO_PIXELFORMAT_R5G6B5        = 610,  // RGBA version of B5G6R5
    ADRENO_PIXELFORMAT_R5G5B5A1      = 611,  // RGBA version of B5G5R5A1
    ADRENO_PIXELFORMAT_R4G4B4A4      = 612,  // RGBA version of B4G4R4A4
    ADRENO_PIXELFORMAT_UYVY          = 614,  // YUV 4:2:2 packed progressive (1 plane)
    ADRENO_PIXELFORMAT_NV21          = 619,
    ADRENO_PIXELFORMAT_Y8U8V8A8      = 620,  // YUV 4:4:4 packed (1 plane)
    ADRENO_PIXELFORMAT_Y8            = 625,  // Single 8-bit luma only channel YUV format
    ADRENO_PIXELFORMAT_TP10          = 654,  // YUV 4:2:0 planar 10 bits/comp (2 planes)
} ADRENOPIXELFORMAT;
