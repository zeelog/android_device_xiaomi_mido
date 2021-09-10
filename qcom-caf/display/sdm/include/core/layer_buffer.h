/*
* Copyright (c) 2014, 2016-2017, The Linux Foundation. All rights reserved.
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

/*! @file layer_buffer.h
  @brief File for layer buffer structure.

*/
#ifndef __LAYER_BUFFER_H__
#define __LAYER_BUFFER_H__

#include <stdint.h>
#include <color_metadata.h>

#include "sdm_types.h"

namespace sdm {

/*! @brief This enum represents display layer inverse gamma correction (IGC) types.

  @sa Layer
*/
enum LayerIGC {
  kIGCNotSpecified,       //!< IGC is not specified.
  kIGCsRGB,               //!< sRGB IGC type.
};

/*! @brief This enum represents different buffer formats supported by display manager.

  @sa LayerBuffer
*/
enum LayerBufferFormat {
  /* All RGB formats, Any new format will be added towards end of this group to maintain backward
     compatibility.
  */
  kFormatARGB8888,      //!< 8-bits Alpha, Red, Green, Blue interleaved in ARGB order.
  kFormatRGBA8888,      //!< 8-bits Red, Green, Blue, Alpha interleaved in RGBA order.
  kFormatBGRA8888,      //!< 8-bits Blue, Green, Red, Alpha interleaved in BGRA order.
  kFormatXRGB8888,      //!< 8-bits Padding, Red, Green, Blue interleaved in XRGB order. No Alpha.
  kFormatRGBX8888,      //!< 8-bits Red, Green, Blue, Padding interleaved in RGBX order. No Alpha.
  kFormatBGRX8888,      //!< 8-bits Blue, Green, Red, Padding interleaved in BGRX order. No Alpha.
  kFormatRGBA5551,      //!< 5-bits Red, Green, Blue, and 1 bit Alpha interleaved in RGBA order.
  kFormatRGBA4444,      //!< 4-bits Red, Green, Blue, Alpha interleaved in RGBA order.
  kFormatRGB888,        //!< 8-bits Red, Green, Blue interleaved in RGB order. No Alpha.
  kFormatBGR888,        //!< 8-bits Blue, Green, Red interleaved in BGR order. No Alpha.
  kFormatRGB565,        //!< 5-bit Red, 6-bit Green, 5-bit Blue interleaved in RGB order. No Alpha.
  kFormatBGR565,        //!< 5-bit Blue, 6-bit Green, 5-bit Red interleaved in BGR order. No Alpha.
  kFormatRGBA8888Ubwc,  //!< UBWC aligned RGBA8888 format
  kFormatRGBX8888Ubwc,  //!< UBWC aligned RGBX8888 format
  kFormatBGR565Ubwc,    //!< UBWC aligned BGR565 format
  kFormatRGBA1010102,   //!< 10-bits Red, Green, Blue, Alpha interleaved in RGBA order.
  kFormatARGB2101010,   //!< 10-bits Alpha, Red, Green, Blue interleaved in ARGB order.
  kFormatRGBX1010102,   //!< 10-bits Red, Green, Blue, Padding interleaved in RGBX order. No Alpha.
  kFormatXRGB2101010,   //!< 10-bits Padding, Red, Green, Blue interleaved in XRGB order. No Alpha.
  kFormatBGRA1010102,   //!< 10-bits Blue, Green, Red, Alpha interleaved in BGRA order.
  kFormatABGR2101010,   //!< 10-bits Alpha, Blue, Green, Red interleaved in ABGR order.
  kFormatBGRX1010102,   //!< 10-bits Blue, Green, Red, Padding interleaved in BGRX order. No Alpha.
  kFormatXBGR2101010,   //!< 10-bits Padding, Blue, Green, Red interleaved in XBGR order. No Alpha.
  kFormatRGBA1010102Ubwc,  //!< UBWC aligned RGBA1010102 format
  kFormatRGBX1010102Ubwc,  //!< UBWC aligned RGBX1010102 format
  kFormatRGB101010,     // 10-bits Red, Green, Blue, interleaved in RGB order. No Alpha.

  /* All YUV-Planar formats, Any new format will be added towards end of this group to maintain
     backward compatibility.
  */
  kFormatYCbCr420Planar = 0x100,  //!< Y-plane: y(0), y(1), y(2) ... y(n)
                                  //!< 2x2 subsampled U-plane: u(0), u(2) ... u(n-1)
                                  //!< 2x2 subsampled V-plane: v(0), v(2) ... v(n-1)

  kFormatYCrCb420Planar,          //!< Y-plane: y(0), y(1), y(2) ... y(n)
                                  //!< 2x2 subsampled V-plane: v(0), v(2) ... v(n-1)
                                  //!< 2x2 subsampled U-plane: u(0), u(2) ... u(n-1)

  kFormatYCrCb420PlanarStride16,  //!< kFormatYCrCb420Planar with stride aligned to 16 bytes

  /* All YUV-Semiplanar formats, Any new format will be added towards end of this group to
     maintain backward compatibility.
  */
  kFormatYCbCr420SemiPlanar = 0x200,  //!< Y-plane: y(0), y(1), y(2) ... y(n)
                                      //!< 2x2 subsampled interleaved UV-plane:
                                      //!<    u(0), v(0), u(2), v(2) ... u(n-1), v(n-1)
                                      //!< aka NV12.

  kFormatYCrCb420SemiPlanar,          //!< Y-plane: y(0), y(1), y(2) ... y(n)
                                      //!< 2x2 subsampled interleaved VU-plane:
                                      //!<    v(0), u(0), v(2), u(2) ... v(n-1), u(n-1)
                                      //!< aka NV21.

  kFormatYCbCr420SemiPlanarVenus,     //!< Y-plane: y(0), y(1), y(2) ... y(n)
                                      //!< 2x2 subsampled interleaved UV-plane:
                                      //!<    u(0), v(0), u(2), v(2) ... u(n-1), v(n-1)

  kFormatYCbCr422H1V2SemiPlanar,      //!< Y-plane: y(0), y(1), y(2) ... y(n)
                                      //!< vertically subsampled interleaved UV-plane:
                                      //!<    u(0), v(1), u(2), v(3) ... u(n-1), v(n)

  kFormatYCrCb422H1V2SemiPlanar,      //!< Y-plane: y(0), y(1), y(2) ... y(n)
                                      //!< vertically subsampled interleaved VU-plane:
                                      //!<    v(0), u(1), v(2), u(3) ... v(n-1), u(n)

  kFormatYCbCr422H2V1SemiPlanar,      //!< Y-plane: y(0), y(1), y(2) ... y(n)
                                      //!< horizontally subsampled interleaved UV-plane:
                                      //!<    u(0), v(1), u(2), v(3) ... u(n-1), v(n)

  kFormatYCrCb422H2V1SemiPlanar,      //!< Y-plane: y(0), y(1), y(2) ... y(n)
                                      //!< horizontally subsampled interleaved VU-plane:
                                      //!<    v(0), u(1), v(2), u(3) ... v(n-1), u(n)

  kFormatYCbCr420SPVenusUbwc,         //!< UBWC aligned YCbCr420SemiPlanarVenus format

  kFormatYCrCb420SemiPlanarVenus,     //!< Y-plane: y(0), y(1), y(2) ... y(n)
                                      //!< 2x2 subsampled interleaved UV-plane:
                                      //!<    v(0), u(0), v(2), u(2) ... v(n-1), u(n-1)

  kFormatYCbCr420P010,                //!< 16 bit Y-plane with 5 MSB bits set to 0:
                                      //!< y(0), y(1), y(2) ... y(n)
                                      //!< 2x2 subsampled interleaved 10 bit UV-plane with
                                      //!< 5 MSB bits set to 0:
                                      //!<    u(0), v(0), u(2), v(2) ... u(n-1), v(n-1)
                                      //!< aka P010.

  kFormatYCbCr420TP10Ubwc,            //!< UBWC aligned YCbCr420TP10 format.

  kFormatYCbCr420P010Ubwc,            //!< UBWC aligned YCbCr420P010 format.

  /* All YUV-Packed formats, Any new format will be added towards end of this group to maintain
     backward compatibility.
  */
  kFormatYCbCr422H2V1Packed = 0x300,  //!< Y-plane interleaved with horizontally subsampled U/V by
                                      //!< factor of 2
                                      //!<    y(0), u(0), y(1), v(0), y(2), u(2), y(3), v(2)
                                      //!<    y(n-1), u(n-1), y(n), v(n-1)

  kFormatCbYCrY422H2V1Packed,
  kFormatInvalid = 0xFFFFFFFF,
};


/*! @brief This enum represents different types of 3D formats supported.

  @sa LayerBufferS3DFormat
*/
enum LayerBufferS3DFormat {
  kS3dFormatNone,            //!< Layer buffer content is not 3D content.
  kS3dFormatLeftRight,       //!< Left and Right view of a 3D content stitched left and right.
  kS3dFormatRightLeft,       //!< Right and Left view of a 3D content stitched left and right.
  kS3dFormatTopBottom,       //!< Left and RightView of a 3D content stitched top and bottom.
  kS3dFormatFramePacking     //!< Left and right view of 3D content coded in consecutive frames.
};

/*! @brief This structure defines a color sample plane belonging to a buffer format. RGB buffer
  formats have 1 plane whereas YUV buffer formats may have upto 4 planes.

  @sa LayerBuffer
*/
struct LayerBufferPlane {
  int fd = -1;           //!< File descriptor referring to the buffer associated with this plane.
  uint32_t offset = 0;   //!< Offset of the plane in bytes from beginning of the buffer.
  uint32_t stride = 0;   //!< Stride in bytes i.e. length of a scanline including padding.
};

/*! @brief This structure defines flags associated with a layer buffer. The 1-bit flag can be set
  to ON(1) or OFF(0).

  @sa LayerBuffer
*/
struct LayerBufferFlags {
  union {
    struct {
      uint32_t secure : 1;          //!< This flag shall be set by client to indicate that the
                                    //!< buffer need to be handled securely.

      uint32_t video  : 1;          //!< This flag shall be set by client to indicate that the
                                    //!< buffer is video/ui buffer.

      uint32_t macro_tile : 1;      //!< This flag shall be set by client to indicate that the
                                    //!< buffer format is macro tiled.

      uint32_t interlace : 1;       //!< This flag shall be set by the client to indicate that
                                    //!< the buffer has interlaced content.

      uint32_t secure_display : 1;  //!< This flag shall be set by the client to indicate that the
                                    //!< secure display session is in progress. Secure display
                                    //!< session can not coexist with non-secure session.

      uint32_t secure_camera : 1;   //!< This flag shall be set by the client to indicate that the
                                    //!< buffer is associated with secure camera session. A secure
                                    //!< camera layer can co-exist with non-secure layer(s).

      uint32_t hdr : 1;             //!< This flag shall be set by the client to indicate that the
                                    //!< the content is HDR.
    };

    uint32_t flags = 0;             //!< For initialization purpose only.
                                    //!< Client shall not refer to it directly.
  };
};

/*! @brief This structure defines a layer buffer handle which contains raw buffer and its associated
  properties.

  @sa LayerBuffer
  @sa LayerStack
*/
struct LayerBuffer {
  uint32_t width = 0;           //!< Aligned width of the Layer that this buffer is for.
  uint32_t height = 0;          //!< Aligned height of the Layer that this buffer is for.
  uint32_t unaligned_width = 0;
                                //!< Unaligned width of the Layer that this buffer is for.
  uint32_t unaligned_height = 0;
                                //!< Unaligned height of the Layer that this buffer is for.
  uint32_t size = 0;            //!< Size of a single buffer (even if multiple clubbed together)
  LayerBufferFormat format = kFormatRGBA8888;     //!< Format of the buffer content.
  ColorMetaData color_metadata = {};              //!< CSC + Range + Transfer + Matrix + HDR Info
  LayerIGC igc = kIGCNotSpecified;                //!< IGC that will be applied on this layer.
  LayerBufferPlane planes[4] = {};
                                //!< Array of planes that this buffer contains. RGB buffer formats
                                //!< have 1 plane whereas YUV buffer formats may have upto 4 planes
                                //!< Total number of planes for the buffer will be interpreted based
                                //!< on the buffer format specified.

  int acquire_fence_fd = -1;    //!< File descriptor referring to a sync fence object which will be
                                //!< signaled when buffer can be read/write by display manager.
                                //!< This fence object is set by the client during Commit(). For
                                //!< input buffers client shall signal this fence when buffer
                                //!< content is available and can be read by display manager. For
                                //!< output buffers, client shall signal fence when buffer is ready
                                //!< to be written by display manager.

                                //!< This field is used only during Commit() and shall be set to -1
                                //!< by the client when buffer is already available for read/write.

  int release_fence_fd = -1;    //!< File descriptor referring to a sync fence object which will be
                                //!< signaled when buffer has been read/written by display manager.
                                //!< This fence object is set by display manager during Commit().
                                //!< For input buffers display manager will signal this fence when
                                //!< buffer has been consumed. For output buffers, display manager
                                //!< will signal this fence when buffer is produced.

                                //!< This field is used only during Commit() and will be set to -1
                                //!< by display manager when buffer is already available for
                                //!< read/write.

  LayerBufferFlags flags;       //!< Flags associated with this buffer.

  LayerBufferS3DFormat s3d_format = kS3dFormatNone;
                                //!< Represents the format of the buffer content in 3D. This field
                                //!< could be modified by both client and SDM.
  uint64_t buffer_id __attribute__((aligned(8))) = 0;
                                //!< Specifies the buffer id.
};

// This enum represents buffer layout types.
enum BufferLayout {
  kLinear,    //!< Linear data
  kUBWC,      //!< UBWC aligned data
  kTPTiled    //!< Tightly Packed data
};

}  // namespace sdm

#endif  // __LAYER_BUFFER_H__

