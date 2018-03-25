/* Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
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
 *
 */

#ifndef MM_LIB2D_H_
#define MM_LIB2D_H_

#include "cam_types.h"
#ifdef QCAMERA_REDEFINE_LOG
#ifndef CAM_MODULE
#define CAM_MODULE CAM_NO_MODULE
#endif
// Camera dependencies
#include "mm_camera_dbg.h"
#endif

/** lib2d_error
 * @MM_LIB2D_SUCCESS: Success
 * @MM_LIB2D_ERR_GENERAL: General Error
 * @MM_LIB2D_ERR_MEMORY: Insufficient memory error
 * @MM_LIB2D_ERR_BAD_PARAM: Bad params error
**/
typedef enum lib2d_error_t {
  MM_LIB2D_SUCCESS,
  MM_LIB2D_ERR_GENERAL,
  MM_LIB2D_ERR_MEMORY,
  MM_LIB2D_ERR_BAD_PARAM,
} lib2d_error;

/** lib2d_mode
 * @MM_LIB2D_SYNC_MODE: Synchronous mode
 * @MM_LIB2D_ASYNC_MODE: Asynchronous mode
**/
typedef enum mm_lib2d_mode_t {
  MM_LIB2D_SYNC_MODE,
  MM_LIB2D_ASYNC_MODE,
} lib2d_mode;

/** mm_lib2d_buffer_type
 * @MM_LIB2D_BUFFER_TYPE_RGB: RGB Buffer type
 * @MM_LIB2D_BUFFER_TYPE_YUV: YUV buffer type
**/
typedef enum mm_lib2d_buffer_type_t {
  MM_LIB2D_BUFFER_TYPE_RGB,
  MM_LIB2D_BUFFER_TYPE_YUV,
} mm_lib2d_buffer_type;

/** mm_lib2d_rgb_buffer
 * @fd: handle to the buffer memory
 * @format: RGB color format
 * @width: defines width in pixels
 * @height: defines height in pixels
 * @buffer: pointer to the RGB buffer
 * @phys: gpu mapped physical address
 * @stride: defines stride in bytes
**/
typedef struct mm_lib2d_rgb_buffer_t {
  int32_t      fd;
  cam_format_t format;
  uint32_t     width;
  uint32_t     height;
  void        *buffer;
  void        *phys;
  int32_t      stride;
} mm_lib2d_rgb_buffer;

/** mm_lib2d_yuv_buffer
 * @fd: handle to the buffer memory
 * @format: YUV color format
 * @width: defines width in pixels
 * @height: defines height in pixels
 * @plane0: holds the whole buffer if YUV format is not planar
 * @phys0: gpu mapped physical address
 * @stride0: stride in bytes
 * @plane1: holds UV or VU plane for planar interleaved
 * @phys2: gpu mapped physical address
 * @stride1: stride in bytes
 * @plane2: holds the 3. plane, ignored if YUV format is not planar
 * @phys2: gpu mapped physical address
 * @stride2: stride in bytes
**/
typedef struct mm_lib2d_yuv_buffer_t {
  int32_t      fd;
  cam_format_t format;
  uint32_t     width;
  uint32_t     height;
  void        *plane0;
  void        *phys0;
  int32_t      stride0;
  void        *plane1;
  void        *phys1;
  int32_t      stride1;
  void        *plane2;
  void        *phys2;
  int32_t      stride2;
} mm_lib2d_yuv_buffer;

/** mm_lib2d_buffer
 * @buffer_type: Buffer type. whether RGB or YUV
 * @rgb_buffer: RGB buffer handle
 * @yuv_buffer: YUV buffer handle
**/
typedef struct mm_lib2d_buffer_t {
  mm_lib2d_buffer_type buffer_type;
  union {
    mm_lib2d_rgb_buffer rgb_buffer;
    mm_lib2d_yuv_buffer yuv_buffer;
  };
} mm_lib2d_buffer;

/** lib2d_client_cb
 * @userdata: App userdata
 * @jobid: job id
**/
typedef lib2d_error (*lib2d_client_cb) (void *userdata, int jobid);

/**
 * Function: mm_lib2d_init
 *
 * Description: Initialization function for Lib2D. src_format, dst_format
 *     are hints to the underlying component to initialize.
 *
 * Input parameters:
 *   mode - Mode (sync/async) in which App wants lib2d to run.
 *   src_format - source surface format
 *   dst_format - Destination surface format
 *   my_obj - handle that will be returned on succesful Init. App has to
 *       call other lib2d functions by passing this handle.
 *
 * Return values:
 *   MM_LIB2D_SUCCESS
 *   MM_LIB2D_ERR_MEMORY
 *   MM_LIB2D_ERR_BAD_PARAM
 *   MM_LIB2D_ERR_GENERAL
 *
 * Notes: none
 **/
lib2d_error mm_lib2d_init(lib2d_mode mode, cam_format_t src_format,
  cam_format_t dst_format, void **lib2d_obj_handle);

/**
 * Function: mm_lib2d_deinit
 *
 * Description: De-Initialization function for Lib2D
 *
 * Input parameters:
 *   lib2d_obj_handle - handle tto the lib2d object
 *
 * Return values:
 *   MM_LIB2D_SUCCESS
 *   MM_LIB2D_ERR_GENERAL
 *
 * Notes: none
 **/
lib2d_error mm_lib2d_deinit(void *lib2d_obj_handle);

/**
 * Function: mm_lib2d_start_job
 *
 * Description: Start executing the job
 *
 * Input parameters:
 *   lib2d_obj_handle - handle tto the lib2d object
 *   src_buffer - pointer to the source buffer
 *   dst_buffer - pointer to the destination buffer
 *   jobid - job id of this request
 *   userdata - userdata that will be pass through callback function
 *   cb - callback function that will be called on completion of this job
 *   rotation - rotation to be applied
 *
 * Return values:
 *   MM_LIB2D_SUCCESS
 *   MM_LIB2D_ERR_MEMORY
 *   MM_LIB2D_ERR_GENERAL
 *
 * Notes: none
 **/
lib2d_error mm_lib2d_start_job(void *lib2d_obj_handle,
    mm_lib2d_buffer* src_buffer, mm_lib2d_buffer* dst_buffer,
    int jobid, void *userdata, lib2d_client_cb cb, uint32_t rotation);

#endif /* MM_LIB2D_H_ */


