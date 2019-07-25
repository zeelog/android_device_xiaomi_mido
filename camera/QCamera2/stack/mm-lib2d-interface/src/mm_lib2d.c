/* Copyright (c) 2015-2017, The Linux Foundation. All rights reserved.
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

// To remove
#include <utils/Log.h>

// System dependencies
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>

// Camera dependencies
#include "img_common.h"
#include "img_comp.h"
#include "img_comp_factory.h"
#include "img_buffer.h"
#include "lib2d.h"
#include "mm_lib2d.h"
#include "img_meta.h"

/** lib2d_job_private_info
 * @jobid: Job id of this process request
 * @userdata: Client userdata that will be passed on callback
 * @lib2d_client_cb: Application's callback function pointer
 *     which will be called upon completion of current job.
**/
typedef struct lib2d_job_private_info_t {
  int   jobid;
  void *userdata;
  lib2d_error (*lib2d_client_cb) (void *userdata, int jobid);
} lib2d_job_private_info;

/** img_lib_t
 * @ptr: handle to imglib library
 * @img_core_get_comp: function pointer for img_core_get_comp
 * @img_wait_for_completion: function pointer for img_wait_for_completion
**/
typedef struct {
  void *ptr;
  int (*img_core_get_comp) (img_comp_role_t role, char *name,
    img_core_ops_t *p_ops);
  int (*img_wait_for_completion) (pthread_cond_t *p_cond,
    pthread_mutex_t *p_mutex, int32_t ms);
} img_lib_t;

/** mm_lib2d_obj
 * @core_ops: image core ops structure handle
 * @comp: component structure handle
 * @comp_mode: underlying component mode
 * @lib2d_mode: lib2d mode requested by client
 * @img_lib: imglib library, function ptrs handle
 * @mutex: lib2d mutex used for synchronization
 * @cond: librd cond used for synchronization
**/
typedef struct mm_lib2d_obj_t {
  img_core_ops_t      core_ops;
  img_component_ops_t comp;
  img_comp_mode_t     comp_mode;
  lib2d_mode          lib2d_mode;
  img_lib_t           img_lib;
  pthread_mutex_t     mutex;
  pthread_cond_t      cond;
} mm_lib2d_obj;


/**
 * Function: lib2d_event_handler
 *
 * Description: Event handler. All the component events
 *     are received here.
 *
 * Input parameters:
 *   p_appdata - lib2d test object
 *   p_event - pointer to the event
 *
 * Return values:
 *   IMG_SUCCESS
 *   IMG_ERR_INVALID_INPUT
 *
 * Notes: none
 **/
int lib2d_event_handler(void* p_appdata, img_event_t *p_event)
{
  mm_lib2d_obj *lib2d_obj = (mm_lib2d_obj *)p_appdata;

  if ((NULL == p_event) || (NULL == p_appdata)) {
    LOGE("invalid event");
    return IMG_ERR_INVALID_INPUT;
  }

  LOGD("type %d", p_event->type);

  switch (p_event->type) {
    case QIMG_EVT_DONE:
      pthread_cond_signal(&lib2d_obj->cond);
      break;
    default:;
  }
  return IMG_SUCCESS;
}

/**
 * Function: lib2d_callback_handler
 *
 * Description: Callback handler. Registered with Component
 *     on IMG_COMP_INIT. Will be called when processing
 *     of current request is completed. If component running in
 *     async mode, this is where client will know the execution
 *     is finished for in, out frames.
 *
 * Input parameters:
 *   p_appdata - lib2d test object
 *   p_in_frame - pointer to input frame
 *   p_out_frame - pointer to output frame
 *   p_meta - pointer to meta data
 *
 * Return values:
 *   IMG_SUCCESS
 *   IMG_ERR_GENERAL
 *
 * Notes: none
 **/
int lib2d_callback_handler(void *userdata, img_frame_t *p_in_frame,
  img_frame_t *p_out_frame, img_meta_t *p_meta)
{
  lib2d_job_private_info *job_info = NULL;

  if (NULL == userdata) {
    LOGE("invalid event");
    return IMG_ERR_INVALID_INPUT;
  }

  // assert(p_in_frame->private_data == p_out_frame->private_data);

  job_info = (lib2d_job_private_info *)p_in_frame->private_data;
  if (job_info->lib2d_client_cb != NULL) {
    job_info->lib2d_client_cb(job_info->userdata, job_info->jobid);
  }

  free(p_in_frame->private_data);
  free(p_in_frame);
  free(p_out_frame);
  free(p_meta);

  return IMG_SUCCESS;
}

/**
 * Function: lib2d_fill_img_frame
 *
 * Description: Setup img_frame_t for given buffer
 *
 * Input parameters:
 *   p_frame - pointer to img_frame_t that needs to be setup
 *   lib2d_buffer - pointer to input buffer
 *   jobid - job id
 *
 * Return values:
 *   MM_LIB2D_SUCCESS
 *   MM_LIB2D_ERR_GENERAL
 *
 * Notes: none
 **/
lib2d_error lib2d_fill_img_frame(img_frame_t *p_frame,
  mm_lib2d_buffer* lib2d_buffer, int jobid)
{
  // use job id for now
  p_frame->frame_cnt = jobid;
  p_frame->idx       = jobid;
  p_frame->frame_id  = jobid;

  if (lib2d_buffer->buffer_type == MM_LIB2D_BUFFER_TYPE_RGB) {
    mm_lib2d_rgb_buffer *rgb_buffer = &lib2d_buffer->rgb_buffer;

    p_frame->info.num_planes = 1;
    p_frame->info.width      = rgb_buffer->width;
    p_frame->info.height     = rgb_buffer->height;

    p_frame->frame[0].plane_cnt = 1;
    p_frame->frame[0].plane[0].plane_type = PLANE_ARGB;
    p_frame->frame[0].plane[0].addr       = rgb_buffer->buffer;
    p_frame->frame[0].plane[0].stride     = rgb_buffer->stride;
    p_frame->frame[0].plane[0].length     = (rgb_buffer->stride *
                                             rgb_buffer->height);
    p_frame->frame[0].plane[0].fd         = rgb_buffer->fd;
    p_frame->frame[0].plane[0].height     = rgb_buffer->height;
    p_frame->frame[0].plane[0].width      = rgb_buffer->width;
    p_frame->frame[0].plane[0].offset     = 0;
    p_frame->frame[0].plane[0].scanline   = rgb_buffer->height;
  } else if (lib2d_buffer->buffer_type == MM_LIB2D_BUFFER_TYPE_YUV) {
    mm_lib2d_yuv_buffer *yuv_buffer = &lib2d_buffer->yuv_buffer;

    p_frame->info.num_planes = 2;
    p_frame->info.width      = yuv_buffer->width;
    p_frame->info.height     = yuv_buffer->height;

    p_frame->frame[0].plane_cnt = 2;
    p_frame->frame[0].plane[0].plane_type = PLANE_Y;
    p_frame->frame[0].plane[0].addr       = yuv_buffer->plane0;
    p_frame->frame[0].plane[0].stride     = yuv_buffer->stride0;
    p_frame->frame[0].plane[0].length     = (yuv_buffer->stride0 *
                                             yuv_buffer->height);
    p_frame->frame[0].plane[0].fd         = yuv_buffer->fd;
    p_frame->frame[0].plane[0].height     = yuv_buffer->height;
    p_frame->frame[0].plane[0].width      = yuv_buffer->width;
    p_frame->frame[0].plane[0].offset     = 0;
    p_frame->frame[0].plane[0].scanline   = yuv_buffer->height;

    if (yuv_buffer->format == CAM_FORMAT_YUV_420_NV12) {
      p_frame->frame[0].plane[1].plane_type = PLANE_CB_CR;
    } else if(yuv_buffer->format == CAM_FORMAT_YUV_420_NV21) {
      p_frame->frame[0].plane[1].plane_type = PLANE_CR_CB;
    }
    p_frame->frame[0].plane[1].addr       = yuv_buffer->plane1;
    p_frame->frame[0].plane[1].stride     = yuv_buffer->stride1;
    p_frame->frame[0].plane[1].length     = (yuv_buffer->stride1 *
                                             yuv_buffer->height / 2);
    p_frame->frame[0].plane[1].fd         = yuv_buffer->fd;
    p_frame->frame[0].plane[1].height     = yuv_buffer->height;
    p_frame->frame[0].plane[1].width      = yuv_buffer->width;
    p_frame->frame[0].plane[1].offset     = 0;
    p_frame->frame[0].plane[1].scanline   = yuv_buffer->height;
  } else {
    return MM_LIB2D_ERR_GENERAL;
  }

  return MM_LIB2D_SUCCESS;
}

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
  cam_format_t dst_format, void **my_obj)
{
  int32_t              rc         = IMG_SUCCESS;
  mm_lib2d_obj        *lib2d_obj  = NULL;
  img_core_ops_t      *p_core_ops = NULL;
  img_component_ops_t *p_comp     = NULL;
  pthread_condattr_t cond_attr;

  if (my_obj == NULL) {
    return MM_LIB2D_ERR_BAD_PARAM;
  }

  // validate src_format, dst_format to check whether we support these.
  // Currently support NV21 to ARGB conversions only. Others not tested.
  if ((src_format != CAM_FORMAT_YUV_420_NV21) ||
    (dst_format != CAM_FORMAT_8888_ARGB)) {
    LOGE("Formats conversion from %d to %d not supported",
        src_format, dst_format);
  }

  lib2d_obj = malloc(sizeof(mm_lib2d_obj));
  if (lib2d_obj == NULL) {
    return MM_LIB2D_ERR_MEMORY;
  }

  // Open libmmcamera_imglib
  lib2d_obj->img_lib.ptr = dlopen("libmmcamera_imglib.so", RTLD_NOW);
  if (!lib2d_obj->img_lib.ptr) {
    LOGE("ERROR: couldn't dlopen libmmcamera_imglib.so: %s",
       dlerror());
    goto FREE_LIB2D_OBJ;
  }

  /* Get function pointer for functions supported by C2D */
  *(void **)&lib2d_obj->img_lib.img_core_get_comp =
      dlsym(lib2d_obj->img_lib.ptr, "img_core_get_comp");
  *(void **)&lib2d_obj->img_lib.img_wait_for_completion =
      dlsym(lib2d_obj->img_lib.ptr, "img_wait_for_completion");

  /* Validate function pointers */
  if ((lib2d_obj->img_lib.img_core_get_comp == NULL) ||
    (lib2d_obj->img_lib.img_wait_for_completion == NULL)) {
    LOGE(" ERROR mapping symbols from libc2d2.so");
    goto FREE_LIB2D_OBJ;
  }

  p_core_ops = &lib2d_obj->core_ops;
  p_comp     = &lib2d_obj->comp;

  pthread_condattr_init(&cond_attr);
  pthread_condattr_setclock(&cond_attr, CLOCK_MONOTONIC);

  pthread_mutex_init(&lib2d_obj->mutex, NULL);
  pthread_cond_init(&lib2d_obj->cond, &cond_attr);
  pthread_condattr_destroy(&cond_attr);

  rc = lib2d_obj->img_lib.img_core_get_comp(IMG_COMP_LIB2D,
    "qti.lib2d", p_core_ops);
  if (rc != IMG_SUCCESS) {
    LOGE("rc %d", rc);
    goto FREE_LIB2D_OBJ;
  }

  rc = IMG_COMP_LOAD(p_core_ops, NULL);
  if (rc != IMG_SUCCESS) {
    LOGE("rc %d", rc);
    goto FREE_LIB2D_OBJ;
  }

  rc = IMG_COMP_CREATE(p_core_ops, p_comp);
  if (rc != IMG_SUCCESS) {
    LOGE("rc %d", rc);
    goto COMP_UNLOAD;
  }

  rc = IMG_COMP_INIT(p_comp, (void *)lib2d_obj, lib2d_callback_handler);
  if (rc != IMG_SUCCESS) {
    LOGE("rc %d", rc);
    goto COMP_UNLOAD;
  }

  rc = IMG_COMP_SET_CB(p_comp, lib2d_event_handler);
  if (rc != IMG_SUCCESS) {
    LOGE("rc %d", rc);
    goto COMP_DEINIT;
  }

  lib2d_obj->lib2d_mode = mode;
  img_comp_mode_t comp_mode;
  if (lib2d_obj->lib2d_mode == MM_LIB2D_SYNC_MODE) {
    comp_mode = IMG_SYNC_MODE;
  } else {
    comp_mode = IMG_ASYNC_MODE;
  }

  // Set source format
  rc = IMG_COMP_SET_PARAM(p_comp, QLIB2D_SOURCE_FORMAT, (void *)&src_format);
  if (rc != IMG_SUCCESS) {
    LOGE("rc %d", rc);
    goto COMP_DEINIT;
  }

  // Set destination format
  rc = IMG_COMP_SET_PARAM(p_comp, QLIB2D_DESTINATION_FORMAT,
    (void *)&dst_format);
  if (rc != IMG_SUCCESS) {
    LOGE("rc %d", rc);
    goto COMP_DEINIT;
  }

  // Try setting the required mode.
  rc = IMG_COMP_SET_PARAM(p_comp, QIMG_PARAM_MODE, (void *)&comp_mode);
  if (rc != IMG_SUCCESS) {
    LOGE("rc %d", rc);
    goto COMP_DEINIT;
  }

  // Get the mode to make sure whether the component is really running
  // in the mode what we set.
  rc = IMG_COMP_GET_PARAM(p_comp, QIMG_PARAM_MODE,
    (void *)&lib2d_obj->comp_mode);
  if (rc != IMG_SUCCESS) {
    LOGE("rc %d", rc);
    goto COMP_DEINIT;
  }

  if (comp_mode != lib2d_obj->comp_mode) {
    LOGD("Component is running in %d mode",
      lib2d_obj->comp_mode);
  }

  *my_obj = (void *)lib2d_obj;

  return MM_LIB2D_SUCCESS;

COMP_DEINIT :
  rc = IMG_COMP_DEINIT(p_comp);
  if (rc != IMG_SUCCESS) {
    LOGE("rc %d", rc);
    return MM_LIB2D_ERR_GENERAL;
  }

COMP_UNLOAD :
  rc = IMG_COMP_UNLOAD(p_core_ops);
  if (rc != IMG_SUCCESS) {
    LOGE("rc %d", rc);
    return MM_LIB2D_ERR_GENERAL;
  }

FREE_LIB2D_OBJ :
  free(lib2d_obj);
  return MM_LIB2D_ERR_GENERAL;
}

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
lib2d_error mm_lib2d_deinit(void *lib2d_obj_handle)
{
  mm_lib2d_obj        *lib2d_obj  = (mm_lib2d_obj *)lib2d_obj_handle;
  int                  rc         = IMG_SUCCESS;
  img_core_ops_t      *p_core_ops = &lib2d_obj->core_ops;
  img_component_ops_t *p_comp     = &lib2d_obj->comp;

  rc = IMG_COMP_DEINIT(p_comp);
  if (rc != IMG_SUCCESS) {
    LOGE("rc %d", rc);
    return MM_LIB2D_ERR_GENERAL;
  }

  rc = IMG_COMP_UNLOAD(p_core_ops);
  if (rc != IMG_SUCCESS) {
    LOGE("rc %d", rc);
    return MM_LIB2D_ERR_GENERAL;
  }

  dlclose(lib2d_obj->img_lib.ptr);
  free(lib2d_obj);

  return MM_LIB2D_SUCCESS;
}

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
  int jobid, void *userdata, lib2d_client_cb cb, uint32_t rotation)
{
  mm_lib2d_obj        *lib2d_obj  = (mm_lib2d_obj *)lib2d_obj_handle;
  int                  rc         = IMG_SUCCESS;
  img_component_ops_t *p_comp     = &lib2d_obj->comp;

  img_frame_t *p_in_frame = malloc(sizeof(img_frame_t));
  if (p_in_frame == NULL) {
    return MM_LIB2D_ERR_MEMORY;
  }

  img_frame_t *p_out_frame = malloc(sizeof(img_frame_t));
  if (p_out_frame == NULL) {
    free(p_in_frame);
    return MM_LIB2D_ERR_MEMORY;
  }

  img_meta_t *p_meta = malloc(sizeof(img_meta_t));
  if (p_meta == NULL) {
    free(p_in_frame);
    free(p_out_frame);
    return MM_LIB2D_ERR_MEMORY;
  }

  lib2d_job_private_info *p_job_info = malloc(sizeof(lib2d_job_private_info));
  if (p_out_frame == NULL) {
    free(p_in_frame);
    free(p_out_frame);
    free(p_meta);
    return MM_LIB2D_ERR_MEMORY;
  }

  memset(p_in_frame,  0x0, sizeof(img_frame_t));
  memset(p_out_frame, 0x0, sizeof(img_frame_t));
  memset(p_meta, 0x0, sizeof(img_meta_t));
  memset(p_job_info,  0x0, sizeof(lib2d_job_private_info));

  // Fill up job info private data structure that can be used in callback to
  // inform back to the client.
  p_job_info->jobid           = jobid;
  p_job_info->userdata        = userdata;
  p_job_info->lib2d_client_cb = cb;

  p_in_frame->private_data  = (void *)p_job_info;
  p_out_frame->private_data = (void *)p_job_info;

  // convert the input info into component understandble data structures

  // Prepare Input, output frames
  lib2d_fill_img_frame(p_in_frame, src_buffer, jobid);
  lib2d_fill_img_frame(p_out_frame, dst_buffer, jobid);

  p_meta->frame_id = jobid;
  p_meta->rotation.device_rotation = (int32_t)rotation;
  p_meta->rotation.frame_rotation = (int32_t)rotation;

  // call set_param to set the source, destination formats

  rc = IMG_COMP_Q_BUF(p_comp, p_in_frame, IMG_IN);
  if (rc != IMG_SUCCESS) {
    LOGE("rc %d", rc);
    goto ERROR;
  }

  rc = IMG_COMP_Q_BUF(p_comp, p_out_frame, IMG_OUT);
  if (rc != IMG_SUCCESS) {
    LOGE("rc %d", rc);
    goto ERROR;
  }

  rc = IMG_COMP_Q_META_BUF(p_comp, p_meta);
  if (rc != IMG_SUCCESS) {
    LOGE("rc %d", rc);
    goto ERROR;
  }

  rc = IMG_COMP_START(p_comp, NULL);
  if (rc != IMG_SUCCESS) {
    LOGE("rc %d", rc);
    goto ERROR;
  }

  if (lib2d_obj->lib2d_mode == MM_LIB2D_SYNC_MODE) {
    if (lib2d_obj->comp_mode == IMG_ASYNC_MODE) {
      LOGD("before wait rc %d", rc);
      rc = lib2d_obj->img_lib.img_wait_for_completion(&lib2d_obj->cond,
        &lib2d_obj->mutex, 10000);
      if (rc != IMG_SUCCESS) {
        LOGE("rc %d", rc);
        goto ERROR;
      }
    }
  }

  rc = IMG_COMP_ABORT(p_comp, NULL);
  if (IMG_ERROR(rc)) {
    LOGE("comp abort failed %d", rc);
    return rc;
  }

  return MM_LIB2D_SUCCESS;
ERROR:
  free(p_in_frame);
  free(p_out_frame);
  free(p_meta);
  free(p_job_info);

  return MM_LIB2D_ERR_GENERAL;
}

