/* Copyright (c) 2012-2016, The Linux Foundation. All rights reserved.
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

// System dependencies
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#define PRCTL_H <SYSTEM_HEADER_PREFIX/prctl.h>
#include PRCTL_H

#ifdef LOAD_ADSP_RPC_LIB
#include <dlfcn.h>
#include <stdlib.h>
#endif

// JPEG dependencies
#include "mm_jpeg_dbg.h"
#include "mm_jpeg_interface.h"
#include "mm_jpeg.h"
#include "mm_jpeg_inlines.h"
#ifdef LIB2D_ROTATION_ENABLE
#include "mm_lib2d.h"
#endif

#define ENCODING_MODE_PARALLEL 1

#define META_KEYFILE QCAMERA_DUMP_FRM_LOCATION"metadata.key"

/**
 * minimal resolution needed for normal mode of ops
 */
#define MM_JPEG_MIN_NOM_RESOLUTION 7680000 /*8MP*/

#ifdef MM_JPEG_USE_PIPELINE
#undef MM_JPEG_CONCURRENT_SESSIONS_COUNT
#define MM_JPEG_CONCURRENT_SESSIONS_COUNT 1
#endif

OMX_ERRORTYPE mm_jpeg_ebd(OMX_HANDLETYPE hComponent,
    OMX_PTR pAppData,
    OMX_BUFFERHEADERTYPE* pBuffer);
OMX_ERRORTYPE mm_jpeg_fbd(OMX_HANDLETYPE hComponent,
    OMX_PTR pAppData,
    OMX_BUFFERHEADERTYPE* pBuffer);
OMX_ERRORTYPE mm_jpeg_event_handler(OMX_HANDLETYPE hComponent,
    OMX_PTR pAppData,
    OMX_EVENTTYPE eEvent,
    OMX_U32 nData1,
    OMX_U32 nData2,
    OMX_PTR pEventData);

static int32_t mm_jpegenc_destroy_job(mm_jpeg_job_session_t *p_session);
static void mm_jpegenc_job_done(mm_jpeg_job_session_t *p_session);
mm_jpeg_job_q_node_t* mm_jpeg_queue_remove_job_by_dst_ptr(
  mm_jpeg_queue_t* queue, void * dst_ptr);
static OMX_ERRORTYPE mm_jpeg_session_configure(mm_jpeg_job_session_t *p_session);

/** mm_jpeg_get_comp_name:
 *
 *  Arguments:
 *       None
 *
 *  Return:
 *       Encoder component name
 *
 *  Description:
 *       Get the name of omx component to be used for jpeg encoding
 *
 **/
inline char* mm_jpeg_get_comp_name()
{
#ifdef MM_JPEG_USE_PIPELINE
  return "OMX.qcom.image.jpeg.encoder_pipeline";
#else
  return "OMX.qcom.image.jpeg.encoder";
#endif
}

/** mm_jpeg_session_send_buffers:
 *
 *  Arguments:
 *    @data: job session
 *
 *  Return:
 *       OMX error values
 *
 *  Description:
 *       Send the buffers to OMX layer
 *
 **/
OMX_ERRORTYPE mm_jpeg_session_send_buffers(void *data)
{
  uint32_t i = 0;
  mm_jpeg_job_session_t* p_session = (mm_jpeg_job_session_t *)data;
  OMX_ERRORTYPE ret = OMX_ErrorNone;
  QOMX_BUFFER_INFO lbuffer_info;
  mm_jpeg_encode_params_t *p_params = &p_session->params;

  memset(&lbuffer_info, 0x0, sizeof(QOMX_BUFFER_INFO));

  if (p_session->lib2d_rotation_flag) {
    for (i = 0; i < p_session->num_src_rot_bufs; i++) {
      lbuffer_info.fd = (OMX_U32)p_session->src_rot_main_buf[i].fd;
      LOGD("Source rot buffer %d", i);
      ret = OMX_UseBuffer(p_session->omx_handle,
        &(p_session->p_in_rot_omx_buf[i]), 0,
        &lbuffer_info, p_session->src_rot_main_buf[i].buf_size,
        p_session->src_rot_main_buf[i].buf_vaddr);
      if (ret) {
        LOGE("Error %d", ret);
        return ret;
      }
    }
  } else {
    for (i = 0; i < p_params->num_src_bufs; i++) {
      LOGD("Source buffer %d", i);
      lbuffer_info.fd = (OMX_U32)p_params->src_main_buf[i].fd;
      ret = OMX_UseBuffer(p_session->omx_handle,
        &(p_session->p_in_omx_buf[i]), 0,
        &lbuffer_info, p_params->src_main_buf[i].buf_size,
        p_params->src_main_buf[i].buf_vaddr);
      if (ret) {
        LOGE("Error %d", ret);
        return ret;
      }
    }
  }

  if (p_session->params.encode_thumbnail) {
    if (p_session->lib2d_rotation_flag && p_session->thumb_from_main) {
      for (i = 0; i < p_session->num_src_rot_bufs; i++) {
        LOGD("Source rot buffer thumb %d", i);
        lbuffer_info.fd = (OMX_U32)p_session->src_rot_main_buf[i].fd;
        ret = OMX_UseBuffer(p_session->omx_handle,
          &(p_session->p_in_rot_omx_thumb_buf[i]), 2,
          &lbuffer_info, p_session->src_rot_main_buf[i].buf_size,
          p_session->src_rot_main_buf[i].buf_vaddr);
        if (ret) {
          LOGE("Error %d", ret);
          return ret;
        }
      }
    } else {
      for (i = 0; i < p_params->num_tmb_bufs; i++) {
        LOGD("Source tmb buffer %d", i);
        lbuffer_info.fd = (OMX_U32)p_params->src_thumb_buf[i].fd;
        ret = OMX_UseBuffer(p_session->omx_handle,
          &(p_session->p_in_omx_thumb_buf[i]), 2,
          &lbuffer_info, p_params->src_thumb_buf[i].buf_size,
        p_params->src_thumb_buf[i].buf_vaddr);
        if (ret) {
          LOGE("Error %d", ret);
          return ret;
        }
      }
    }
  }

  for (i = 0; i < p_params->num_dst_bufs; i++) {
    LOGD("Dest buffer %d", i);
    lbuffer_info.fd = (OMX_U32)p_params->dest_buf[i].fd;
    ret = OMX_UseBuffer(p_session->omx_handle, &(p_session->p_out_omx_buf[i]),
      1, &lbuffer_info, p_params->dest_buf[i].buf_size,
      p_params->dest_buf[i].buf_vaddr);
    if (ret) {
      LOGE("Error");
      return ret;
    }
  }

  return ret;
}


/** mm_jpeg_session_free_buffers:
 *
 *  Arguments:
 *    @data: job session
 *
 *  Return:
 *       OMX error values
 *
 *  Description:
 *       Free the buffers from OMX layer
 *
 **/
OMX_ERRORTYPE mm_jpeg_session_free_buffers(void *data)
{
  OMX_ERRORTYPE ret = OMX_ErrorNone;
  uint32_t i = 0;
  mm_jpeg_job_session_t* p_session = (mm_jpeg_job_session_t *)data;
  mm_jpeg_encode_params_t *p_params = &p_session->params;


  if (p_session->lib2d_rotation_flag) {
    for (i = 0; i < p_session->num_src_rot_bufs; i++) {
      LOGD("Source rot buffer %d", i);
      ret = OMX_FreeBuffer(p_session->omx_handle, 0,
        p_session->p_in_rot_omx_buf[i]);
      if (ret) {
        LOGE("Error %d", ret);
        return ret;
      }
    }
  } else {
    for (i = 0; i < p_params->num_src_bufs; i++) {
      LOGD("Source buffer %d", i);
      ret = OMX_FreeBuffer(p_session->omx_handle, 0,
        p_session->p_in_omx_buf[i]);
      if (ret) {
        LOGE("Error %d", ret);
        return ret;
      }
    }
  }

  if (p_session->params.encode_thumbnail) {
    if (p_session->lib2d_rotation_flag && p_session->thumb_from_main) {
      for (i = 0; i < p_session->num_src_rot_bufs; i++) {
        LOGD("Source rot buffer thumb %d", i);
        ret = OMX_FreeBuffer(p_session->omx_handle, 2,
        p_session->p_in_rot_omx_thumb_buf[i]);
        if (ret) {
          LOGE("Error %d", ret);
          return ret;
        }
      }
    } else {
      for (i = 0; i < p_params->num_tmb_bufs; i++) {
        LOGD("Source buffer %d", i);
        ret = OMX_FreeBuffer(p_session->omx_handle, 2,
          p_session->p_in_omx_thumb_buf[i]);
        if (ret) {
          LOGE("Error %d", ret);
          return ret;
        }
      }
    }
  }

  for (i = 0; i < p_params->num_dst_bufs; i++) {
    LOGD("Dest buffer %d", i);
    ret = OMX_FreeBuffer(p_session->omx_handle, 1, p_session->p_out_omx_buf[i]);
    if (ret) {
      LOGE("Error");
      return ret;
    }
  }
  return ret;
}




/** mm_jpeg_session_change_state:
 *
 *  Arguments:
 *    @p_session: job session
 *    @new_state: new state to be transitioned to
 *    @p_exec: transition function
 *
 *  Return:
 *       OMX error values
 *
 *  Description:
 *       This method is used for state transition
 *
 **/
OMX_ERRORTYPE mm_jpeg_session_change_state(mm_jpeg_job_session_t* p_session,
  OMX_STATETYPE new_state,
  mm_jpeg_transition_func_t p_exec)
{
  OMX_ERRORTYPE ret = OMX_ErrorNone;
  OMX_STATETYPE current_state;
  LOGD("new_state %d p_exec %p",
    new_state, p_exec);


  pthread_mutex_lock(&p_session->lock);

  ret = OMX_GetState(p_session->omx_handle, &current_state);

  if (ret) {
    pthread_mutex_unlock(&p_session->lock);
    return ret;
  }

  if (current_state == new_state) {
    pthread_mutex_unlock(&p_session->lock);
    return OMX_ErrorNone;
  }

  p_session->state_change_pending = OMX_TRUE;
  pthread_mutex_unlock(&p_session->lock);
  ret = OMX_SendCommand(p_session->omx_handle, OMX_CommandStateSet,
    new_state, NULL);
  pthread_mutex_lock(&p_session->lock);
  if (ret) {
    LOGE("Error %d", ret);
    pthread_mutex_unlock(&p_session->lock);
    return OMX_ErrorIncorrectStateTransition;
  }
  if ((OMX_ErrorNone != p_session->error_flag) &&
      (OMX_ErrorOverflow != p_session->error_flag)) {
    LOGE("Error %d", p_session->error_flag);
    pthread_mutex_unlock(&p_session->lock);
    return p_session->error_flag;
  }
  if (p_exec) {
    ret = p_exec(p_session);
    if (ret) {
      LOGE("Error %d", ret);
      pthread_mutex_unlock(&p_session->lock);
      return ret;
    }
  }
  if (p_session->state_change_pending) {
    LOGL("before wait");
    pthread_cond_wait(&p_session->cond, &p_session->lock);
    LOGL("after wait");
  }
  pthread_mutex_unlock(&p_session->lock);
  return ret;
}

/** mm_jpeg_session_create:
 *
 *  Arguments:
 *    @p_session: job session
 *
 *  Return:
 *       OMX error types
 *
 *  Description:
 *       Create a jpeg encode session
 *
 **/
OMX_ERRORTYPE mm_jpeg_session_create(mm_jpeg_job_session_t* p_session)
{
  OMX_ERRORTYPE rc = OMX_ErrorNone;
  mm_jpeg_obj *my_obj = (mm_jpeg_obj *) p_session->jpeg_obj;

  pthread_mutex_init(&p_session->lock, NULL);
  pthread_cond_init(&p_session->cond, NULL);
  cirq_reset(&p_session->cb_q);
  p_session->state_change_pending = OMX_FALSE;
  p_session->abort_state = MM_JPEG_ABORT_NONE;
  p_session->error_flag = OMX_ErrorNone;
  p_session->ebd_count = 0;
  p_session->fbd_count = 0;
  p_session->encode_pid = -1;
  p_session->config = OMX_FALSE;
  p_session->exif_count_local = 0;
  p_session->auto_out_buf = OMX_FALSE;

  p_session->omx_callbacks.EmptyBufferDone = mm_jpeg_ebd;
  p_session->omx_callbacks.FillBufferDone = mm_jpeg_fbd;
  p_session->omx_callbacks.EventHandler = mm_jpeg_event_handler;

  p_session->thumb_from_main = 0;
#ifdef MM_JPEG_USE_PIPELINE
  p_session->thumb_from_main = !p_session->params.thumb_from_postview;
#endif

  rc = OMX_GetHandle(&p_session->omx_handle,
      mm_jpeg_get_comp_name(),
      (void *)p_session,
      &p_session->omx_callbacks);
  if (OMX_ErrorNone != rc) {
    LOGE("OMX_GetHandle failed (%d)", rc);
    return rc;
  }

  my_obj->num_sessions++;

  return rc;
}



/** mm_jpeg_session_destroy:
 *
 *  Arguments:
 *    @p_session: job session
 *
 *  Return:
 *       none
 *
 *  Description:
 *       Destroy a jpeg encode session
 *
 **/
void mm_jpeg_session_destroy(mm_jpeg_job_session_t* p_session)
{
  OMX_ERRORTYPE rc = OMX_ErrorNone;
  OMX_STATETYPE state;
  uint32_t i;
  mm_jpeg_obj *my_obj = (mm_jpeg_obj *) p_session->jpeg_obj;

  LOGD("E");
  if (NULL == p_session->omx_handle) {
    LOGE("invalid handle");
    return;
  }

  rc = OMX_GetState(p_session->omx_handle, &state);

  //Check state before state transition
  if ((state == OMX_StateExecuting) || (state == OMX_StatePause)) {
    rc = mm_jpeg_session_change_state(p_session, OMX_StateIdle, NULL);
    if (rc) {
      LOGE("Error");
    }
  }

  rc = OMX_GetState(p_session->omx_handle, &state);

  if (state == OMX_StateIdle) {
    rc = mm_jpeg_session_change_state(p_session, OMX_StateLoaded,
      mm_jpeg_session_free_buffers);
    if (rc) {
      LOGE("Error");
    }
  }

  if (p_session->lib2d_rotation_flag) {
    for (i = 0; i < p_session->num_src_rot_bufs; i++) {
      if (p_session->src_rot_ion_buffer[i].addr) {
        buffer_deallocate(&p_session->src_rot_ion_buffer[i]);
      }
    }
  }

  /* If current session is the session in progress
     set session in progress pointer to null*/
  p_session->config = OMX_FALSE;
  if (my_obj->p_session_inprogress == p_session) {
    my_obj->p_session_inprogress = NULL;
  }

  rc = OMX_FreeHandle(p_session->omx_handle);
  if (0 != rc) {
    LOGE("OMX_FreeHandle failed (%d)", rc);
  }
  p_session->omx_handle = NULL;

  pthread_mutex_destroy(&p_session->lock);
  pthread_cond_destroy(&p_session->cond);

  if (NULL != p_session->meta_enc_key) {
    free(p_session->meta_enc_key);
    p_session->meta_enc_key = NULL;
  }

  my_obj->num_sessions--;

  // Destroy next session
  if (p_session->next_session) {
    mm_jpeg_session_destroy(p_session->next_session);
  }

  LOGD("Session destroy successful. X");
}



/** mm_jpeg_session_config_main_buffer_offset:
 *
 *  Arguments:
 *    @p_session: job session
 *
 *  Return:
 *       OMX error values
 *
 *  Description:
 *       Configure the buffer offsets
 *
 **/
OMX_ERRORTYPE mm_jpeg_session_config_main_buffer_offset(
  mm_jpeg_job_session_t* p_session)
{
  OMX_ERRORTYPE rc = 0;
  OMX_INDEXTYPE buffer_index;
  QOMX_YUV_FRAME_INFO frame_info;
  size_t totalSize = 0;
  mm_jpeg_encode_params_t *p_params = &p_session->params;

  mm_jpeg_buf_t *p_src_buf =
    &p_params->src_main_buf[0];

  memset(&frame_info, 0x0, sizeof(QOMX_YUV_FRAME_INFO));

  frame_info.cbcrStartOffset[0] = p_src_buf->offset.mp[0].len;
  frame_info.cbcrStartOffset[1] = p_src_buf->offset.mp[1].len;
  if (!p_session->lib2d_rotation_flag) {
    frame_info.yOffset = p_src_buf->offset.mp[0].offset;
    frame_info.cbcrOffset[0] = p_src_buf->offset.mp[1].offset;
    frame_info.cbcrOffset[1] = p_src_buf->offset.mp[2].offset;
  }
  totalSize = p_src_buf->buf_size;

  rc = OMX_GetExtensionIndex(p_session->omx_handle,
    QOMX_IMAGE_EXT_BUFFER_OFFSET_NAME, &buffer_index);
  if (rc != OMX_ErrorNone) {
    LOGE("Failed");
    return rc;
  }

  LOGD("yOffset = %d, cbcrOffset = (%d %d), totalSize = %zd,"
    "cbcrStartOffset = (%d %d)",
    (int)frame_info.yOffset,
    (int)frame_info.cbcrOffset[0],
    (int)frame_info.cbcrOffset[1],
    totalSize,
    (int)frame_info.cbcrStartOffset[0],
    (int)frame_info.cbcrStartOffset[1]);

  rc = OMX_SetParameter(p_session->omx_handle, buffer_index, &frame_info);
  if (rc != OMX_ErrorNone) {
    LOGE("Failed");
    return rc;
  }
  return rc;
}

/** mm_jpeg_encoding_mode:
 *
 *  Arguments:
 *    @p_session: job session
 *
 *  Return:
 *       OMX error values
 *
 *  Description:
 *       Configure the serial or parallel encoding
 *       mode
 *
 **/
OMX_ERRORTYPE mm_jpeg_encoding_mode(
  mm_jpeg_job_session_t* p_session)
{
  OMX_ERRORTYPE rc = 0;
  OMX_INDEXTYPE indextype;
  QOMX_ENCODING_MODE encoding_mode;

  rc = OMX_GetExtensionIndex(p_session->omx_handle,
    QOMX_IMAGE_EXT_ENCODING_MODE_NAME, &indextype);
  if (rc != OMX_ErrorNone) {
    LOGE("Failed");
    return rc;
  }

  if (ENCODING_MODE_PARALLEL) {
    encoding_mode = OMX_Parallel_Encoding;
  } else {
    encoding_mode = OMX_Serial_Encoding;
  }
  LOGD("encoding mode = %d ",
    (int)encoding_mode);
  rc = OMX_SetParameter(p_session->omx_handle, indextype, &encoding_mode);
  if (rc != OMX_ErrorNone) {
    LOGE("Failed");
    return rc;
  }
  return rc;
}

/** mm_jpeg_get_speed:
 *
 *  Arguments:
 *    @p_session: job session
 *
 *  Return:
 *       ops speed type for jpeg
 *
 *  Description:
 *      Configure normal or high speed jpeg
 *
 **/
QOMX_JPEG_SPEED_MODE mm_jpeg_get_speed(
  mm_jpeg_job_session_t* p_session)
{
  mm_jpeg_encode_params_t *p_params = &p_session->params;
  cam_dimension_t *p_dim = &p_params->main_dim.src_dim;
  if (p_params->burst_mode ||
    (MM_JPEG_MIN_NOM_RESOLUTION < (p_dim->width * p_dim->height))) {
    return QOMX_JPEG_SPEED_MODE_HIGH;
  }
  return QOMX_JPEG_SPEED_MODE_NORMAL;
}

/** mm_jpeg_speed_mode:
 *
 *  Arguments:
 *    @p_session: job session
 *
 *  Return:
 *       OMX error values
 *
 *  Description:
 *      Configure normal or high speed jpeg
 *
 **/
OMX_ERRORTYPE mm_jpeg_speed_mode(
  mm_jpeg_job_session_t* p_session)
{
  OMX_ERRORTYPE rc = 0;
  OMX_INDEXTYPE indextype;
  QOMX_JPEG_SPEED jpeg_speed;

  rc = OMX_GetExtensionIndex(p_session->omx_handle,
    QOMX_IMAGE_EXT_JPEG_SPEED_NAME, &indextype);
  if (rc != OMX_ErrorNone) {
    LOGE("Failed");
    return rc;
  }

  jpeg_speed.speedMode = mm_jpeg_get_speed(p_session);
  LOGH("speed %d", jpeg_speed.speedMode);

  rc = OMX_SetParameter(p_session->omx_handle, indextype, &jpeg_speed);
  if (rc != OMX_ErrorNone) {
    LOGE("Failed");
    return rc;
  }
  return rc;
}

/** mm_jpeg_get_mem:
 *
 *  Arguments:
 *    @p_out_buf : jpeg output buffer
 *    @p_jpeg_session: job session
 *
 *  Return:
 *       0 for success else failure
 *
 *  Description:
 *      gets the jpeg output buffer
 *
 **/
static int32_t mm_jpeg_get_mem(
  omx_jpeg_ouput_buf_t *p_out_buf, void* p_jpeg_session)
{
  int32_t rc = 0;
  mm_jpeg_job_session_t *p_session = (mm_jpeg_job_session_t *)p_jpeg_session;
  mm_jpeg_encode_params_t *p_params = NULL;
  mm_jpeg_encode_job_t *p_encode_job = NULL;

  if (!p_session) {
    LOGE("Invalid input");
    return -1;
  }
  p_params = &p_session->params;
  p_encode_job = &p_session->encode_job;
  if (!p_params || !p_encode_job || !p_params->get_memory) {
    LOGE("Invalid jpeg encode params");
    return -1;
  }
  p_params->get_memory(p_out_buf);
  p_encode_job->ref_count++;
  p_encode_job->alloc_out_buffer = p_out_buf;
  LOGD("ref_count %d p_out_buf %p",
    p_encode_job->ref_count, p_out_buf);
  return rc;
}

/** mm_jpeg_put_mem:
 *
 *  Arguments:
 *    @p_jpeg_session: job session
 *
 *  Return:
 *       0 for success else failure
 *
 *  Description:
 *      releases the jpeg output buffer
 *
 **/
static int32_t mm_jpeg_put_mem(void* p_jpeg_session)
{
  int32_t rc = 0;
  mm_jpeg_job_session_t *p_session = (mm_jpeg_job_session_t *)p_jpeg_session;
  mm_jpeg_encode_params_t *p_params = NULL;
  mm_jpeg_encode_job_t *p_encode_job = NULL;

  if (!p_session) {
    LOGE("Invalid input");
    return -1;
  }
  p_params = &p_session->params;
  p_encode_job = &p_session->encode_job;

  if (!p_params->get_memory) {
    LOGD("get_mem not defined, ignore put mem");
    return 0;
  }
  if (!p_params || !p_encode_job || !p_params->put_memory) {
    LOGE("Invalid jpeg encode params");
    return -1;
  }
  if ((MM_JPEG_ABORT_NONE != p_session->abort_state) &&
    p_encode_job->ref_count) {
    omx_jpeg_ouput_buf_t *p_out_buf =
      ( omx_jpeg_ouput_buf_t *) p_encode_job->alloc_out_buffer;
    p_params->put_memory(p_out_buf);
    p_encode_job->ref_count--;
    p_encode_job->alloc_out_buffer = NULL;
  } else if (p_encode_job->ref_count) {
    p_encode_job->ref_count--;
  } else {
    LOGW("Buffer already released %d", p_encode_job->ref_count);
    rc = -1;
  }
  LOGD("ref_count %d p_out_buf %p",
    p_encode_job->ref_count, p_encode_job->alloc_out_buffer);
  return rc;
}

/** mm_jpeg_mem_ops:
 *
 *  Arguments:
 *    @p_session: job session
 *
 *  Return:
 *       OMX error values
 *
 *  Description:
 *       Configure the serial or parallel encoding
 *       mode
 *
 **/
OMX_ERRORTYPE mm_jpeg_mem_ops(
  mm_jpeg_job_session_t* p_session)
{
  OMX_ERRORTYPE rc = 0;
  OMX_INDEXTYPE indextype;
  QOMX_MEM_OPS mem_ops;
  mm_jpeg_encode_params_t *p_params = &p_session->params;

  if (p_params->get_memory) {
    mem_ops.get_memory = mm_jpeg_get_mem;
  } else {
    mem_ops.get_memory = NULL;
    LOGH("HAL get_mem handler undefined");
  }

  mem_ops.psession = p_session;
  rc = OMX_GetExtensionIndex(p_session->omx_handle,
    QOMX_IMAGE_EXT_MEM_OPS_NAME, &indextype);
  if (rc != OMX_ErrorNone) {
    LOGE("Failed");
    return rc;
  }

  rc = OMX_SetParameter(p_session->omx_handle, indextype, &mem_ops);
  if (rc != OMX_ErrorNone) {
    LOGE("Failed");
    return rc;
  }
  return rc;
}

/** mm_jpeg_metadata:
 *
 *  Arguments:
 *    @p_session: job session
 *
 *  Return:
 *       OMX error values
 *
 *  Description:
 *       Pass meta data
 *
 **/
OMX_ERRORTYPE mm_jpeg_metadata(
  mm_jpeg_job_session_t* p_session)
{
  OMX_ERRORTYPE rc = OMX_ErrorNone;
  OMX_INDEXTYPE indexType;
  QOMX_METADATA lMeta;
  mm_jpeg_encode_job_t *p_jobparams = &p_session->encode_job;
  mm_jpeg_obj *my_obj = (mm_jpeg_obj *) p_session->jpeg_obj;

  rc = OMX_GetExtensionIndex(p_session->omx_handle,
      QOMX_IMAGE_EXT_METADATA_NAME, &indexType);

  if (rc != OMX_ErrorNone) {
    LOGE("Failed");
    return rc;
  }

  lMeta.metadata = (OMX_U8 *)p_jobparams->p_metadata;
  lMeta.metaPayloadSize = sizeof(*p_jobparams->p_metadata);
  lMeta.mobicat_mask = p_jobparams->mobicat_mask;
  lMeta.static_metadata = (OMX_U8 *)my_obj->jpeg_metadata;

  rc = OMX_SetConfig(p_session->omx_handle, indexType, &lMeta);
  if (rc != OMX_ErrorNone) {
    LOGE("Failed");
    return rc;
  }
  return OMX_ErrorNone;
}

/** mm_jpeg_meta_enc_key:
 *
 *  Arguments:
 *    @p_session: job session
 *
 *  Return:
 *       OMX error values
 *
 *  Description:
 *       Pass metadata encrypt key
 *
 **/
OMX_ERRORTYPE mm_jpeg_meta_enc_key(
  mm_jpeg_job_session_t* p_session)
{
  OMX_ERRORTYPE rc = OMX_ErrorNone;
  OMX_INDEXTYPE indexType;
  QOMX_META_ENC_KEY lKey;

  lKey.metaKey = p_session->meta_enc_key;
  lKey.keyLen = p_session->meta_enc_keylen;

  if ((!lKey.metaKey) || (!lKey.keyLen)){
    LOGD("Key is invalid");
    return OMX_ErrorNone;
  }

  rc = OMX_GetExtensionIndex(p_session->omx_handle,
      QOMX_IMAGE_EXT_META_ENC_KEY_NAME, &indexType);

  if (rc != OMX_ErrorNone) {
    LOGE("Failed");
    return rc;
  }

  rc = OMX_SetConfig(p_session->omx_handle, indexType, &lKey);
  if (rc != OMX_ErrorNone) {
    LOGE("Failed");
    return rc;
  }
  return OMX_ErrorNone;
}

/** map_jpeg_format:
 *
 *  Arguments:
 *    @color_fmt: color format
 *
 *  Return:
 *       OMX color format
 *
 *  Description:
 *       Map mmjpeg color format to OMX color format
 *
 **/
int map_jpeg_format(mm_jpeg_color_format color_fmt)
{
  switch (color_fmt) {
  case MM_JPEG_COLOR_FORMAT_YCRCBLP_H2V2:
    return (int)OMX_QCOM_IMG_COLOR_FormatYVU420SemiPlanar;
  case MM_JPEG_COLOR_FORMAT_YCBCRLP_H2V2:
    return (int)OMX_COLOR_FormatYUV420SemiPlanar;
  case MM_JPEG_COLOR_FORMAT_YCRCBLP_H2V1:
    return (int)OMX_QCOM_IMG_COLOR_FormatYVU422SemiPlanar;
  case MM_JPEG_COLOR_FORMAT_YCBCRLP_H2V1:
    return (int)OMX_COLOR_FormatYUV422SemiPlanar;
  case MM_JPEG_COLOR_FORMAT_YCRCBLP_H1V2:
    return (int)OMX_QCOM_IMG_COLOR_FormatYVU422SemiPlanar_h1v2;
  case MM_JPEG_COLOR_FORMAT_YCBCRLP_H1V2:
    return (int)OMX_QCOM_IMG_COLOR_FormatYUV422SemiPlanar_h1v2;
  case MM_JPEG_COLOR_FORMAT_YCRCBLP_H1V1:
    return (int)OMX_QCOM_IMG_COLOR_FormatYVU444SemiPlanar;
  case MM_JPEG_COLOR_FORMAT_YCBCRLP_H1V1:
    return (int)OMX_QCOM_IMG_COLOR_FormatYUV444SemiPlanar;
  case MM_JPEG_COLOR_FORMAT_MONOCHROME:
     return (int)OMX_COLOR_FormatMonochrome;
  default:
    LOGW("invalid format %d", color_fmt);
    return (int)OMX_QCOM_IMG_COLOR_FormatYVU420SemiPlanar;
  }
}

/** mm_jpeg_get_imgfmt_from_colorfmt:
 *
 *  Arguments:
 *    @color_fmt: color format
 *
 *  Return:
 *    cam format
 *
 *  Description:
 *    Get camera image format from color format
 *
 **/
cam_format_t mm_jpeg_get_imgfmt_from_colorfmt
  (mm_jpeg_color_format color_fmt)
{
  switch (color_fmt) {
  case MM_JPEG_COLOR_FORMAT_MONOCHROME:
    return CAM_FORMAT_Y_ONLY;
  case MM_JPEG_COLOR_FORMAT_YCRCBLP_H2V2:
    return CAM_FORMAT_YUV_420_NV21;
  case MM_JPEG_COLOR_FORMAT_YCBCRLP_H2V2:
    return CAM_FORMAT_YUV_420_NV12;
  case MM_JPEG_COLOR_FORMAT_YCRCBLP_H2V1:
  case MM_JPEG_COLOR_FORMAT_YCRCBLP_H1V2:
    return CAM_FORMAT_YUV_422_NV61;
  case MM_JPEG_COLOR_FORMAT_YCBCRLP_H2V1:
  case MM_JPEG_COLOR_FORMAT_YCBCRLP_H1V2:
    return CAM_FORMAT_YUV_422_NV16;
  case MM_JPEG_COLOR_FORMAT_YCRCBLP_H1V1:
    return CAM_FORMAT_YUV_444_NV42;
  case MM_JPEG_COLOR_FORMAT_YCBCRLP_H1V1:
    return CAM_FORMAT_YUV_444_NV24;
  default:
    return CAM_FORMAT_Y_ONLY;
  }
}

/** mm_jpeg_session_config_port:
 *
 *  Arguments:
 *    @p_session: job session
 *
 *  Return:
 *       OMX error values
 *
 *  Description:
 *       Configure OMX ports
 *
 **/
OMX_ERRORTYPE mm_jpeg_session_config_ports(mm_jpeg_job_session_t* p_session)
{
  OMX_ERRORTYPE ret = OMX_ErrorNone;
  mm_jpeg_encode_params_t *p_params = &p_session->params;
  OMX_CONFIG_ROTATIONTYPE rotate;

  mm_jpeg_buf_t *p_src_buf =
    &p_params->src_main_buf[0];

  p_session->inputPort.nPortIndex = 0;
  p_session->outputPort.nPortIndex = 1;
  p_session->inputTmbPort.nPortIndex = 2;

  ret = OMX_GetParameter(p_session->omx_handle, OMX_IndexParamPortDefinition,
    &p_session->inputPort);
  if (ret) {
    LOGE("failed");
    return ret;
  }

  ret = OMX_GetParameter(p_session->omx_handle, OMX_IndexParamPortDefinition,
    &p_session->inputTmbPort);
  if (ret) {
    LOGE("failed");
    return ret;
  }

  ret = OMX_GetParameter(p_session->omx_handle, OMX_IndexParamPortDefinition,
    &p_session->outputPort);
  if (ret) {
    LOGE("failed");
    return ret;
  }

  if (p_session->lib2d_rotation_flag &&
    ((p_session->params.rotation == 90) ||
    (p_session->params.rotation == 270))) {
    p_session->inputPort.format.image.nFrameWidth =
      (OMX_U32)p_params->main_dim.src_dim.height;
    p_session->inputPort.format.image.nFrameHeight =
      (OMX_U32)p_params->main_dim.src_dim.width;
    p_session->inputPort.format.image.nStride =
      p_src_buf->offset.mp[0].scanline;
    p_session->inputPort.format.image.nSliceHeight =
      (OMX_U32)p_src_buf->offset.mp[0].stride;
  } else {
    p_session->inputPort.format.image.nFrameWidth =
      (OMX_U32)p_params->main_dim.src_dim.width;
    p_session->inputPort.format.image.nFrameHeight =
      (OMX_U32)p_params->main_dim.src_dim.height;
    p_session->inputPort.format.image.nStride =
      p_src_buf->offset.mp[0].stride;
    p_session->inputPort.format.image.nSliceHeight =
      (OMX_U32)p_src_buf->offset.mp[0].scanline;
  }

  p_session->inputPort.format.image.eColorFormat =
    map_jpeg_format(p_params->color_format);
  p_session->inputPort.nBufferSize =
    p_params->src_main_buf[0/*p_jobparams->src_index*/].buf_size;

  if (p_session->lib2d_rotation_flag) {
    p_session->inputPort.nBufferCountActual =
      (OMX_U32)p_session->num_src_rot_bufs;
  } else {
    p_session->inputPort.nBufferCountActual =
      (OMX_U32)p_params->num_src_bufs;
  }

  ret = OMX_SetParameter(p_session->omx_handle, OMX_IndexParamPortDefinition,
    &p_session->inputPort);
  if (ret) {
    LOGE("failed");
    return ret;
  }

  if (p_session->params.encode_thumbnail) {
    mm_jpeg_buf_t *p_tmb_buf =
      &p_params->src_thumb_buf[0];
    if ((p_session->lib2d_rotation_flag && p_session->thumb_from_main) &&
      ((p_session->params.rotation == 90) ||
      (p_session->params.rotation == 270))) {
      p_session->inputTmbPort.format.image.nFrameWidth =
        (OMX_U32)p_params->thumb_dim.src_dim.height;
      p_session->inputTmbPort.format.image.nFrameHeight =
        (OMX_U32)p_params->thumb_dim.src_dim.width;
      p_session->inputTmbPort.format.image.nStride =
        p_tmb_buf->offset.mp[0].scanline;
      p_session->inputTmbPort.format.image.nSliceHeight =
        (OMX_U32)p_tmb_buf->offset.mp[0].stride;
    } else {
      p_session->inputTmbPort.format.image.nFrameWidth =
        (OMX_U32)p_params->thumb_dim.src_dim.width;
      p_session->inputTmbPort.format.image.nFrameHeight =
        (OMX_U32)p_params->thumb_dim.src_dim.height;
      p_session->inputTmbPort.format.image.nStride =
        p_tmb_buf->offset.mp[0].stride;
      p_session->inputTmbPort.format.image.nSliceHeight =
        (OMX_U32)p_tmb_buf->offset.mp[0].scanline;
    }

    p_session->inputTmbPort.format.image.eColorFormat =
      map_jpeg_format(p_params->thumb_color_format);
    p_session->inputTmbPort.nBufferSize =
      p_params->src_thumb_buf[0].buf_size;

    if (p_session->lib2d_rotation_flag && p_session->thumb_from_main) {
      p_session->inputTmbPort.nBufferCountActual =
        (OMX_U32)p_session->num_src_rot_bufs;
    } else {
      p_session->inputTmbPort.nBufferCountActual =
        (OMX_U32)p_params->num_tmb_bufs;
    }

    ret = OMX_SetParameter(p_session->omx_handle, OMX_IndexParamPortDefinition,
      &p_session->inputTmbPort);

    if (ret) {
      LOGE("failed");
      return ret;
    }

    // Enable thumbnail port
    ret = OMX_SendCommand(p_session->omx_handle, OMX_CommandPortEnable,
        p_session->inputTmbPort.nPortIndex, NULL);

    if (ret) {
      LOGE("failed");
      return ret;
    }
  } else {
    // Disable thumbnail port
    ret = OMX_SendCommand(p_session->omx_handle, OMX_CommandPortDisable,
        p_session->inputTmbPort.nPortIndex, NULL);

    if (ret) {
      LOGE("failed");
      return ret;
    }
  }

  p_session->outputPort.nBufferSize =
    p_params->dest_buf[0].buf_size;
  p_session->outputPort.nBufferCountActual = (OMX_U32)p_params->num_dst_bufs;
  ret = OMX_SetParameter(p_session->omx_handle, OMX_IndexParamPortDefinition,
    &p_session->outputPort);
  if (ret) {
    LOGE("failed");
    return ret;
  }

  /* set rotation */
  memset(&rotate, 0, sizeof(rotate));
  rotate.nPortIndex = 1;

  if (p_session->lib2d_rotation_flag) {
    rotate.nRotation = 0;
  } else {
    rotate.nRotation = (OMX_S32)p_params->rotation;
  }

  ret = OMX_SetConfig(p_session->omx_handle, OMX_IndexConfigCommonRotate,
      &rotate);
  if (OMX_ErrorNone != ret) {
    LOGE("Error %d", ret);
    return ret;
  }
  LOGD("Set rotation to %d at port_idx = %d",
      (int)p_params->rotation, (int)rotate.nPortIndex);

  return ret;
}

/** mm_jpeg_update_thumbnail_crop
 *
 *  Arguments:
 *    @p_thumb_dim: thumbnail dimension
 *    @crop_width : flag indicating if width needs to be cropped
 *
 *  Return:
 *    OMX error values
 *
 *  Description:
 *    Updates thumbnail crop aspect ratio based on
 *    thumbnail destination aspect ratio.
 *
 */
OMX_ERRORTYPE mm_jpeg_update_thumbnail_crop(mm_jpeg_dim_t *p_thumb_dim,
  uint8_t crop_width)
{
  OMX_ERRORTYPE ret = OMX_ErrorNone;
  int32_t cropped_width = 0, cropped_height = 0;

  if (crop_width) {
    // Keep height constant
    cropped_height = p_thumb_dim->crop.height;
    cropped_width = floor((cropped_height * p_thumb_dim->dst_dim.width) /
      p_thumb_dim->dst_dim.height);
    if (cropped_width % 2) {
      cropped_width -= 1;
    }
  } else {
    // Keep width constant
    cropped_width = p_thumb_dim->crop.width;
    cropped_height = floor((cropped_width * p_thumb_dim->dst_dim.height) /
      p_thumb_dim->dst_dim.width);
    if (cropped_height % 2) {
      cropped_height -= 1;
    }
  }
  p_thumb_dim->crop.left = p_thumb_dim->crop.left +
    floor((p_thumb_dim->crop.width - cropped_width) / 2);
  if (p_thumb_dim->crop.left % 2) {
    p_thumb_dim->crop.left -= 1;
  }
  p_thumb_dim->crop.top = p_thumb_dim->crop.top +
    floor((p_thumb_dim->crop.height - cropped_height) / 2);
  if (p_thumb_dim->crop.top % 2) {
    p_thumb_dim->crop.top -= 1;
  }
  p_thumb_dim->crop.width = cropped_width;
  p_thumb_dim->crop.height = cropped_height;

  LOGH("New thumbnail crop: left %d, top %d, crop width %d,"
    " crop height %d", p_thumb_dim->crop.left,
    p_thumb_dim->crop.top, p_thumb_dim->crop.width,
    p_thumb_dim->crop.height);

  return ret;
}

/** mm_jpeg_omx_config_thumbnail:
 *
 *  Arguments:
 *    @p_session: job session
 *
 *  Return:
 *       OMX error values
 *
 *  Description:
 *       Configure OMX ports
 *
 **/
OMX_ERRORTYPE mm_jpeg_session_config_thumbnail(mm_jpeg_job_session_t* p_session)
{
  OMX_ERRORTYPE ret = OMX_ErrorNone;
  QOMX_THUMBNAIL_INFO thumbnail_info;
  OMX_INDEXTYPE thumb_indextype;
  mm_jpeg_encode_params_t *p_params = &p_session->params;
  mm_jpeg_encode_job_t *p_jobparams = &p_session->encode_job;
  mm_jpeg_dim_t *p_thumb_dim = &p_jobparams->thumb_dim;
  mm_jpeg_dim_t *p_main_dim = &p_jobparams->main_dim;
  QOMX_YUV_FRAME_INFO *p_frame_info = &thumbnail_info.tmbOffset;
  mm_jpeg_buf_t *p_tmb_buf = &p_params->src_thumb_buf[p_jobparams->thumb_index];

  LOGH("encode_thumbnail %u",
    p_params->encode_thumbnail);
  if (OMX_FALSE == p_params->encode_thumbnail) {
    return ret;
  }

  if ((p_thumb_dim->dst_dim.width == 0) || (p_thumb_dim->dst_dim.height == 0)) {
    LOGE("Error invalid output dim for thumbnail");
    return OMX_ErrorBadParameter;
  }

  if ((p_thumb_dim->src_dim.width == 0) || (p_thumb_dim->src_dim.height == 0)) {
    LOGE("Error invalid input dim for thumbnail");
    return OMX_ErrorBadParameter;
  }

  if ((p_thumb_dim->crop.width == 0) || (p_thumb_dim->crop.height == 0)) {
    p_thumb_dim->crop.width = p_thumb_dim->src_dim.width;
    p_thumb_dim->crop.height = p_thumb_dim->src_dim.height;
  }

  /* check crop boundary */
  if ((p_thumb_dim->crop.width + p_thumb_dim->crop.left > p_thumb_dim->src_dim.width) ||
    (p_thumb_dim->crop.height + p_thumb_dim->crop.top > p_thumb_dim->src_dim.height)) {
    LOGE("invalid crop boundary (%d, %d) offset (%d, %d) out of (%d, %d)",
      p_thumb_dim->crop.width,
      p_thumb_dim->crop.height,
      p_thumb_dim->crop.left,
      p_thumb_dim->crop.top,
      p_thumb_dim->src_dim.width,
      p_thumb_dim->src_dim.height);
    return OMX_ErrorBadParameter;
  }

  memset(&thumbnail_info, 0x0, sizeof(QOMX_THUMBNAIL_INFO));
  ret = OMX_GetExtensionIndex(p_session->omx_handle,
    QOMX_IMAGE_EXT_THUMBNAIL_NAME,
    &thumb_indextype);
  if (ret) {
    LOGE("Error %d", ret);
    return ret;
  }

  /* fill thumbnail info */
  thumbnail_info.scaling_enabled = 1;
  thumbnail_info.input_width = (OMX_U32)p_thumb_dim->src_dim.width;
  thumbnail_info.input_height = (OMX_U32)p_thumb_dim->src_dim.height;
  thumbnail_info.rotation = (OMX_U32)p_params->thumb_rotation;
  thumbnail_info.quality = (OMX_U32)p_params->thumb_quality;
  thumbnail_info.output_width = (OMX_U32)p_thumb_dim->dst_dim.width;
  thumbnail_info.output_height = (OMX_U32)p_thumb_dim->dst_dim.height;

  if (p_session->thumb_from_main) {

    if (p_session->lib2d_rotation_flag) {
      thumbnail_info.rotation = 0;
    } else {
      if ((p_session->params.thumb_rotation == 90 ||
        p_session->params.thumb_rotation == 270) &&
        (p_session->params.rotation == 0 ||
        p_session->params.rotation == 180)) {

        thumbnail_info.output_width = (OMX_U32)p_thumb_dim->dst_dim.height;
        thumbnail_info.output_height = (OMX_U32)p_thumb_dim->dst_dim.width;
        thumbnail_info.rotation = p_session->params.rotation;
      }
    }

    //Thumb FOV should be within main image FOV
    if (p_thumb_dim->crop.left < p_main_dim->crop.left) {
      p_thumb_dim->crop.left = p_main_dim->crop.left;
    }

    if (p_thumb_dim->crop.top < p_main_dim->crop.top) {
      p_thumb_dim->crop.top = p_main_dim->crop.top;
    }

    while ((p_thumb_dim->crop.left + p_thumb_dim->crop.width) >
      (p_main_dim->crop.left + p_main_dim->crop.width)) {
      if (p_thumb_dim->crop.left == p_main_dim->crop.left) {
        p_thumb_dim->crop.width = p_main_dim->crop.width;
      } else {
        p_thumb_dim->crop.left = p_main_dim->crop.left;
      }
    }

    while ((p_thumb_dim->crop.top + p_thumb_dim->crop.height) >
      (p_main_dim->crop.top + p_main_dim->crop.height)) {
      if (p_thumb_dim->crop.top == p_main_dim->crop.top) {
        p_thumb_dim->crop.height = p_main_dim->crop.height;
      } else {
        p_thumb_dim->crop.top = p_main_dim->crop.top;
      }
    }
  } else if ((p_thumb_dim->dst_dim.width > p_thumb_dim->src_dim.width) ||
    (p_thumb_dim->dst_dim.height > p_thumb_dim->src_dim.height)) {
    LOGE("Incorrect thumbnail dim %dx%d resetting to %dx%d", p_thumb_dim->dst_dim.width,
      p_thumb_dim->dst_dim.height, p_thumb_dim->src_dim.width,
      p_thumb_dim->src_dim.height);
    thumbnail_info.output_width = (OMX_U32)p_thumb_dim->src_dim.width;
    thumbnail_info.output_height = (OMX_U32)p_thumb_dim->src_dim.height;
  }

  // If the thumbnail crop aspect ratio image and thumbnail dest aspect
  // ratio are different, reset the thumbnail crop
  double thumbcrop_aspect_ratio = (double)p_thumb_dim->crop.width /
    (double)p_thumb_dim->crop.height;
  double thumbdst_aspect_ratio = (double)p_thumb_dim->dst_dim.width /
    (double)p_thumb_dim->dst_dim.height;
  if ((thumbdst_aspect_ratio - thumbcrop_aspect_ratio) >
    ASPECT_TOLERANCE) {
    mm_jpeg_update_thumbnail_crop(p_thumb_dim, 0);
  } else if ((thumbcrop_aspect_ratio - thumbdst_aspect_ratio) >
    ASPECT_TOLERANCE) {
    mm_jpeg_update_thumbnail_crop(p_thumb_dim, 1);
  }

  // Fill thumbnail crop info
  thumbnail_info.crop_info.nWidth = (OMX_U32)p_thumb_dim->crop.width;
  thumbnail_info.crop_info.nHeight = (OMX_U32)p_thumb_dim->crop.height;
  thumbnail_info.crop_info.nLeft = p_thumb_dim->crop.left;
  thumbnail_info.crop_info.nTop = p_thumb_dim->crop.top;

  memset(p_frame_info, 0x0, sizeof(*p_frame_info));

  p_frame_info->cbcrStartOffset[0] = p_tmb_buf->offset.mp[0].len;
  p_frame_info->cbcrStartOffset[1] = p_tmb_buf->offset.mp[1].len;
  p_frame_info->yOffset = p_tmb_buf->offset.mp[0].offset;
  p_frame_info->cbcrOffset[0] = p_tmb_buf->offset.mp[1].offset;
  p_frame_info->cbcrOffset[1] = p_tmb_buf->offset.mp[2].offset;

  if (p_session->lib2d_rotation_flag && p_session->thumb_from_main) {
    p_frame_info->yOffset = 0;
    p_frame_info->cbcrOffset[0] = 0;
    p_frame_info->cbcrOffset[1] = 0;
  }

  ret = OMX_SetConfig(p_session->omx_handle, thumb_indextype,
    &thumbnail_info);
  if (ret) {
    LOGE("Error");
    return ret;
  }

  return ret;
}

/** mm_jpeg_session_config_main_crop:
 *
 *  Arguments:
 *    @p_session: job session
 *
 *  Return:
 *       OMX error values
 *
 *  Description:
 *       Configure main image crop
 *
 **/
OMX_ERRORTYPE mm_jpeg_session_config_main_crop(mm_jpeg_job_session_t *p_session)
{
  OMX_CONFIG_RECTTYPE rect_type_in, rect_type_out;
  OMX_ERRORTYPE ret = OMX_ErrorNone;
  mm_jpeg_encode_job_t *p_jobparams = &p_session->encode_job;
  mm_jpeg_dim_t *dim = &p_jobparams->main_dim;

  if ((dim->crop.width == 0) || (dim->crop.height == 0)) {
    dim->crop.width = dim->src_dim.width;
    dim->crop.height = dim->src_dim.height;
  }
  /* error check first */
  if ((dim->crop.width + dim->crop.left > dim->src_dim.width) ||
    (dim->crop.height + dim->crop.top > dim->src_dim.height)) {
    LOGE("invalid crop boundary (%d, %d) out of (%d, %d)",
      dim->crop.width + dim->crop.left,
      dim->crop.height + dim->crop.top,
      dim->src_dim.width,
      dim->src_dim.height);
    return OMX_ErrorBadParameter;
  }

  memset(&rect_type_in, 0, sizeof(rect_type_in));
  memset(&rect_type_out, 0, sizeof(rect_type_out));
  rect_type_in.nPortIndex = 0;
  rect_type_out.nPortIndex = 0;

  if ((dim->src_dim.width != dim->crop.width) ||
    (dim->src_dim.height != dim->crop.height) ||
    (dim->src_dim.width != dim->dst_dim.width) ||
    (dim->src_dim.height != dim->dst_dim.height)) {
    /* Scaler information */
    rect_type_in.nWidth = CEILING2(dim->crop.width);
    rect_type_in.nHeight = CEILING2(dim->crop.height);
    rect_type_in.nLeft = dim->crop.left;
    rect_type_in.nTop = dim->crop.top;

    if (dim->dst_dim.width && dim->dst_dim.height) {
      rect_type_out.nWidth = (OMX_U32)dim->dst_dim.width;
      rect_type_out.nHeight = (OMX_U32)dim->dst_dim.height;
    }
  }

  ret = OMX_SetConfig(p_session->omx_handle, OMX_IndexConfigCommonInputCrop,
    &rect_type_in);
  if (OMX_ErrorNone != ret) {
    LOGE("Error");
    return ret;
  }

  LOGH("OMX_IndexConfigCommonInputCrop w = %d, h = %d, l = %d, t = %d,"
    " port_idx = %d",
    (int)rect_type_in.nWidth, (int)rect_type_in.nHeight,
    (int)rect_type_in.nLeft, (int)rect_type_in.nTop,
    (int)rect_type_in.nPortIndex);

  ret = OMX_SetConfig(p_session->omx_handle, OMX_IndexConfigCommonOutputCrop,
    &rect_type_out);
  if (OMX_ErrorNone != ret) {
    LOGE("Error");
    return ret;
  }
  LOGD("OMX_IndexConfigCommonOutputCrop w = %d, h = %d,"
    " port_idx = %d",
    (int)rect_type_out.nWidth, (int)rect_type_out.nHeight,
    (int)rect_type_out.nPortIndex);

  return ret;
}

/** mm_jpeg_session_config_main:
 *
 *  Arguments:
 *    @p_session: job session
 *
 *  Return:
 *       OMX error values
 *
 *  Description:
 *       Configure main image
 *
 **/
OMX_ERRORTYPE mm_jpeg_session_config_main(mm_jpeg_job_session_t *p_session)
{
  OMX_ERRORTYPE rc = OMX_ErrorNone;

  /* config port */
  LOGD("config port");
  rc = mm_jpeg_session_config_ports(p_session);
  if (OMX_ErrorNone != rc) {
    LOGE("config port failed");
    return rc;
  }

  /* config buffer offset */
  LOGD("config main buf offset");
  rc = mm_jpeg_session_config_main_buffer_offset(p_session);
  if (OMX_ErrorNone != rc) {
    LOGE("config buffer offset failed");
    return rc;
  }

  /* set the encoding mode */
  rc = mm_jpeg_encoding_mode(p_session);
  if (OMX_ErrorNone != rc) {
    LOGE("config encoding mode failed");
    return rc;
  }

  /* set the metadata encrypt key */
  rc = mm_jpeg_meta_enc_key(p_session);
  if (OMX_ErrorNone != rc) {
    LOGE("config session failed");
    return rc;
  }

  /* set the mem ops */
  rc = mm_jpeg_mem_ops(p_session);
  if (OMX_ErrorNone != rc) {
    LOGE("config mem ops failed");
    return rc;
  }
  /* set the jpeg speed mode */
  rc = mm_jpeg_speed_mode(p_session);
  if (OMX_ErrorNone != rc) {
    LOGE("config speed mode failed");
    return rc;
  }

  return rc;
}

/** mm_jpeg_session_config_common:
 *
 *  Arguments:
 *    @p_session: job session
 *
 *  Return:
 *       OMX error values
 *
 *  Description:
 *       Configure common parameters
 *
 **/
OMX_ERRORTYPE mm_jpeg_session_config_common(mm_jpeg_job_session_t *p_session)
{
  OMX_ERRORTYPE rc = OMX_ErrorNone;
  OMX_INDEXTYPE exif_idx;
  OMX_CONFIG_ROTATIONTYPE rotate;
  mm_jpeg_encode_job_t *p_jobparams = &p_session->encode_job;
  QOMX_EXIF_INFO exif_info;

  /* set rotation */
  memset(&rotate, 0, sizeof(rotate));
  rotate.nPortIndex = 1;

  if (p_session->lib2d_rotation_flag) {
    rotate.nRotation = 0;
  } else {
    rotate.nRotation = (OMX_S32)p_jobparams->rotation;
  }

  rc = OMX_SetConfig(p_session->omx_handle, OMX_IndexConfigCommonRotate,
    &rotate);
  if (OMX_ErrorNone != rc) {
      LOGE("Error %d", rc);
      return rc;
  }
  LOGD("Set rotation to %d at port_idx = %d",
    (int)p_jobparams->rotation, (int)rotate.nPortIndex);

  /* Set Exif data*/
  memset(&p_session->exif_info_local[0], 0, sizeof(p_session->exif_info_local));
  rc = OMX_GetExtensionIndex(p_session->omx_handle, QOMX_IMAGE_EXT_EXIF_NAME,
    &exif_idx);
  if (OMX_ErrorNone != rc) {
    LOGE("Error %d", rc);
    return rc;
  }

  LOGD("Num of exif entries passed from HAL: %d",
      (int)p_jobparams->exif_info.numOfEntries);
  if (p_jobparams->exif_info.numOfEntries > 0) {
    rc = OMX_SetConfig(p_session->omx_handle, exif_idx,
        &p_jobparams->exif_info);
    if (OMX_ErrorNone != rc) {
      LOGE("Error %d", rc);
      return rc;
    }
  }
  /*parse aditional exif data from the metadata*/
  exif_info.numOfEntries = 0;
  exif_info.exif_data = &p_session->exif_info_local[0];
  process_meta_data(p_jobparams->p_metadata, &exif_info,
    &p_jobparams->cam_exif_params, p_jobparams->hal_version);
  /* After Parse metadata */
  p_session->exif_count_local = (int)exif_info.numOfEntries;

  if (exif_info.numOfEntries > 0) {
    /* set exif tags */
    LOGD("exif tags from metadata count %d",
      (int)exif_info.numOfEntries);

    rc = OMX_SetConfig(p_session->omx_handle, exif_idx,
      &exif_info);
    if (OMX_ErrorNone != rc) {
      LOGE("Error %d", rc);
      return rc;
    }
  }

  return rc;
}

/** mm_jpeg_session_abort:
 *
 *  Arguments:
 *    @p_session: jpeg session
 *
 *  Return:
 *       OMX_BOOL
 *
 *  Description:
 *       Abort ongoing job
 *
 **/
OMX_BOOL mm_jpeg_session_abort(mm_jpeg_job_session_t *p_session)
{
  OMX_ERRORTYPE ret = OMX_ErrorNone;
  int rc = 0;

  LOGD("E");
  pthread_mutex_lock(&p_session->lock);
  if (MM_JPEG_ABORT_NONE != p_session->abort_state) {
    pthread_mutex_unlock(&p_session->lock);
    LOGH("**** ALREADY ABORTED");
    return 0;
  }
  p_session->abort_state = MM_JPEG_ABORT_INIT;
  if (OMX_TRUE == p_session->encoding) {
    p_session->state_change_pending = OMX_TRUE;

    LOGH("**** ABORTING");
    pthread_mutex_unlock(&p_session->lock);

    ret = OMX_SendCommand(p_session->omx_handle, OMX_CommandStateSet,
      OMX_StateIdle, NULL);

    if (ret != OMX_ErrorNone) {
      LOGE("OMX_SendCommand returned error %d", ret);
      return 1;
    }
    rc = mm_jpegenc_destroy_job(p_session);
    if (rc != 0) {
      LOGE("Destroy job returned error %d", rc);
    }

    pthread_mutex_lock(&p_session->lock);
    if (MM_JPEG_ABORT_INIT == p_session->abort_state) {
      LOGL("before wait");
      pthread_cond_wait(&p_session->cond, &p_session->lock);
    }
    LOGL("after wait");
  }
  p_session->abort_state = MM_JPEG_ABORT_DONE;

  mm_jpeg_put_mem((void *)p_session);

  pthread_mutex_unlock(&p_session->lock);

  // Abort next session
  if (p_session->next_session) {
    mm_jpeg_session_abort(p_session->next_session);
  }

  LOGD("X");
  return 0;
}

/** mm_jpeg_config_multi_image_info
 *
 *  Arguments:
 *    @p_session: encode session
 *
 *  Return: OMX_ERRORTYPE
 *
 *  Description:
 *       Configure multi image parameters
 *
 **/
static OMX_ERRORTYPE mm_jpeg_config_multi_image_info(
  mm_jpeg_job_session_t *p_session)
{
  OMX_ERRORTYPE ret = OMX_ErrorNone;
  QOMX_JPEG_MULTI_IMAGE_INFO multi_image_info;
  OMX_INDEXTYPE multi_image_index;
  mm_jpeg_encode_job_t *p_jobparams = &p_session->encode_job;

  ret = OMX_GetExtensionIndex(p_session->omx_handle,
    QOMX_IMAGE_EXT_MULTI_IMAGE_NAME, &multi_image_index);
  if (ret) {
    LOGE("Error getting multi image info extention index %d", ret);
    return ret;
  }
  memset(&multi_image_info, 0, sizeof(multi_image_info));
  if (p_jobparams->multi_image_info.type == MM_JPEG_TYPE_MPO) {
    multi_image_info.image_type = QOMX_JPEG_IMAGE_TYPE_MPO;
  } else {
    multi_image_info.image_type = QOMX_JPEG_IMAGE_TYPE_JPEG;
  }
  multi_image_info.is_primary_image = p_jobparams->multi_image_info.is_primary;
  multi_image_info.num_of_images = p_jobparams->multi_image_info.num_of_images;
  multi_image_info.enable_metadata = p_jobparams->multi_image_info.enable_metadata;

  ret = OMX_SetConfig(p_session->omx_handle, multi_image_index,
    &multi_image_info);
  if (ret) {
    LOGE("Error setting multi image config");
    return ret;
  }
  return ret;
}

/** mm_jpeg_configure_params
 *
 *  Arguments:
 *    @p_session: encode session
 *
 *  Return:
 *       none
 *
 *  Description:
 *       Configure the job specific params
 *
 **/
static OMX_ERRORTYPE mm_jpeg_configure_job_params(
  mm_jpeg_job_session_t *p_session)
{
  OMX_ERRORTYPE ret = OMX_ErrorNone;
  OMX_IMAGE_PARAM_QFACTORTYPE q_factor;
  QOMX_WORK_BUFFER work_buffer;
  OMX_INDEXTYPE work_buffer_index;
  mm_jpeg_encode_params_t *p_params = &p_session->params;
  mm_jpeg_encode_job_t *p_jobparams = &p_session->encode_job;
  int i;

  /* common config */
  ret = mm_jpeg_session_config_common(p_session);
  if (OMX_ErrorNone != ret) {
    LOGE("config common failed");
  }

  /* config Main Image crop */
  LOGD("config main crop");
  ret = mm_jpeg_session_config_main_crop(p_session);
  if (OMX_ErrorNone != ret) {
    LOGE("config crop failed");
    return ret;
  }

  /* set quality */
  memset(&q_factor, 0, sizeof(q_factor));
  q_factor.nPortIndex = 0;
  q_factor.nQFactor = p_params->quality;
  ret = OMX_SetConfig(p_session->omx_handle, OMX_IndexParamQFactor, &q_factor);
  LOGD("config QFactor: %d", (int)q_factor.nQFactor);
  if (OMX_ErrorNone != ret) {
    LOGE("Error setting Q factor %d", ret);
    return ret;
  }

  /* config thumbnail */
  ret = mm_jpeg_session_config_thumbnail(p_session);
  if (OMX_ErrorNone != ret) {
    LOGE("config thumbnail img failed");
    return ret;
  }

  //Pass the ION buffer to be used as o/p for HW
  memset(&work_buffer, 0x0, sizeof(QOMX_WORK_BUFFER));
  ret = OMX_GetExtensionIndex(p_session->omx_handle,
    QOMX_IMAGE_EXT_WORK_BUFFER_NAME,
    &work_buffer_index);
  if (ret) {
    LOGE("Error getting work buffer index %d", ret);
    return ret;
  }
  work_buffer.fd = p_session->work_buffer.p_pmem_fd;
  work_buffer.vaddr = p_session->work_buffer.addr;
  work_buffer.length = (uint32_t)p_session->work_buffer.size;
  LOGH("Work buffer info %d %p WorkBufSize: %d invalidate",
      work_buffer.fd, work_buffer.vaddr, work_buffer.length);

  buffer_invalidate(&p_session->work_buffer);

  ret = OMX_SetConfig(p_session->omx_handle, work_buffer_index,
    &work_buffer);
  if (ret) {
    LOGE("Error");
    return ret;
  }

  /* set metadata */
  ret = mm_jpeg_metadata(p_session);
  if (OMX_ErrorNone != ret) {
    LOGE("config makernote data failed");
    return ret;
  }

  /* set QTable */
  for (i = 0; i < QTABLE_MAX; i++) {
    if (p_jobparams->qtable_set[i]) {
      ret = OMX_SetConfig(p_session->omx_handle,
        OMX_IndexParamQuantizationTable, &p_jobparams->qtable[i]);
      if (OMX_ErrorNone != ret) {
        LOGE("set QTable Error");
        return ret;
      }
    }
  }

  /* Set multi image data*/
  ret = mm_jpeg_config_multi_image_info(p_session);
  if (OMX_ErrorNone != ret) {
    LOGE("config multi image data failed");
    return ret;
  }

  return ret;
}

/** mm_jpeg_session_configure:
 *
 *  Arguments:
 *    @data: encode session
 *
 *  Return:
 *       none
 *
 *  Description:
 *       Configure the session
 *
 **/
static OMX_ERRORTYPE mm_jpeg_session_configure(mm_jpeg_job_session_t *p_session)
{
  OMX_ERRORTYPE ret = OMX_ErrorNone;

  LOGD("E ");

  MM_JPEG_CHK_ABORT(p_session, ret, error);

  /* config main img */
  ret = mm_jpeg_session_config_main(p_session);
  if (OMX_ErrorNone != ret) {
    LOGE("config main img failed");
    goto error;
  }
  ret = mm_jpeg_session_change_state(p_session, OMX_StateIdle,
    mm_jpeg_session_send_buffers);
  if (ret) {
    LOGE("change state to idle failed %d", ret);
    goto error;
  }

  ret = mm_jpeg_session_change_state(p_session, OMX_StateExecuting,
    NULL);
  if (ret) {
    LOGE("change state to executing failed %d", ret);
    goto error;
  }

error:
  LOGD("X ret %d", ret);
  return ret;
}






/** mm_jpeg_session_encode:
 *
 *  Arguments:
 *    @p_session: encode session
 *
 *  Return:
 *       OMX_ERRORTYPE
 *
 *  Description:
 *       Start the encoding
 *
 **/
static OMX_ERRORTYPE mm_jpeg_session_encode(mm_jpeg_job_session_t *p_session)
{
  OMX_ERRORTYPE ret = OMX_ErrorNone;
  mm_jpeg_encode_job_t *p_jobparams = &p_session->encode_job;
  mm_jpeg_obj *my_obj = (mm_jpeg_obj *) p_session->jpeg_obj;
  OMX_BUFFERHEADERTYPE *p_in_buf = NULL;
  OMX_BUFFERHEADERTYPE *p_in_thumb_buf = NULL;

  pthread_mutex_lock(&p_session->lock);
  p_session->abort_state = MM_JPEG_ABORT_NONE;
  p_session->encoding = OMX_FALSE;
  pthread_mutex_unlock(&p_session->lock);

  if (p_session->thumb_from_main) {
    if (0 > p_jobparams->src_index) {
      LOGE("Error");
      ret = OMX_ErrorUnsupportedIndex;
      goto error;
    }
    p_jobparams->thumb_index = (uint32_t)p_jobparams->src_index;
    p_jobparams->thumb_dim.crop = p_jobparams->main_dim.crop;
  }

  if (OMX_FALSE == p_session->config) {
    /* If another session in progress clear that sessions configuration */
    if (my_obj->p_session_inprogress != NULL) {
      OMX_STATETYPE state;
      mm_jpeg_job_session_t *p_session_inprogress = my_obj->p_session_inprogress;

      OMX_GetState(p_session_inprogress->omx_handle, &state);

      //Check state before state transition
      if ((state == OMX_StateExecuting) || (state == OMX_StatePause)) {
        ret = mm_jpeg_session_change_state(p_session_inprogress,
          OMX_StateIdle, NULL);
        if (ret) {
          LOGE("Error");
          goto error;
        }
      }

      OMX_GetState(p_session_inprogress->omx_handle, &state);

      if (state == OMX_StateIdle) {
        ret = mm_jpeg_session_change_state(p_session_inprogress,
          OMX_StateLoaded, mm_jpeg_session_free_buffers);
        if (ret) {
          LOGE("Error");
          goto error;
        }
      }
      p_session_inprogress->config = OMX_FALSE;
      my_obj->p_session_inprogress = NULL;
    }

    ret = mm_jpeg_session_configure(p_session);
    if (ret) {
      LOGE("Error");
      goto error;
    }
    p_session->config = OMX_TRUE;
    my_obj->p_session_inprogress = p_session;
  }

  ret = mm_jpeg_configure_job_params(p_session);
  if (ret) {
      LOGE("Error");
      goto error;
  }
  pthread_mutex_lock(&p_session->lock);
  p_session->encoding = OMX_TRUE;
  pthread_mutex_unlock(&p_session->lock);

  MM_JPEG_CHK_ABORT(p_session, ret, error);

  if (p_session->lib2d_rotation_flag) {
    p_in_buf = p_session->p_in_rot_omx_buf[p_jobparams->src_index];
  } else {
    p_in_buf = p_session->p_in_omx_buf[p_jobparams->src_index];
  }

#ifdef MM_JPEG_DUMP_INPUT
  char filename[256];
  snprintf(filename, sizeof(filename),
    QCAMERA_DUMP_FRM_LOCATION"jpeg/mm_jpeg_int%d.yuv", p_session->ebd_count);
  DUMP_TO_FILE(filename, p_in_buf->pBuffer, (size_t)p_in_buf->nAllocLen);
#endif
  ret = OMX_EmptyThisBuffer(p_session->omx_handle, p_in_buf);
  if (ret) {
    LOGE("Error");
    goto error;
  }

  if (p_session->params.encode_thumbnail) {

    if (p_session->thumb_from_main &&
      p_session->lib2d_rotation_flag) {
      p_in_thumb_buf = p_session->p_in_rot_omx_thumb_buf[p_jobparams->thumb_index];
    } else {
      p_in_thumb_buf = p_session->p_in_omx_thumb_buf[p_jobparams->thumb_index];
    }

#ifdef MM_JPEG_DUMP_INPUT
    char thumb_filename[FILENAME_MAX];
    snprintf(thumb_filename, sizeof(thumb_filename),
      QCAMERA_DUMP_FRM_LOCATION"jpeg/mm_jpeg_int_t%d.yuv", p_session->ebd_count);
    DUMP_TO_FILE(filename, p_in_thumb_buf->pBuffer,
      (size_t)p_in_thumb_buf->nAllocLen);
#endif
    ret = OMX_EmptyThisBuffer(p_session->omx_handle, p_in_thumb_buf);
    if (ret) {
      LOGE("Error");
      goto error;
    }
  }

  ret = OMX_FillThisBuffer(p_session->omx_handle,
    p_session->p_out_omx_buf[p_jobparams->dst_index]);
  if (ret) {
    LOGE("Error");
    goto error;
  }

  MM_JPEG_CHK_ABORT(p_session, ret, error);

error:

  LOGD("X ");
  return ret;
}

/** mm_jpeg_process_encoding_job:
 *
 *  Arguments:
 *    @my_obj: jpeg client
 *    @job_node: job node
 *
 *  Return:
 *       0 for success -1 otherwise
 *
 *  Description:
 *       Start the encoding job
 *
 **/
int32_t mm_jpeg_process_encoding_job(mm_jpeg_obj *my_obj, mm_jpeg_job_q_node_t* job_node)
{
  mm_jpeg_q_data_t qdata;
  int32_t rc = 0;
  OMX_ERRORTYPE ret = OMX_ErrorNone;
  mm_jpeg_job_session_t *p_session = NULL;
  uint32_t buf_idx;

  /* check if valid session */
  p_session = mm_jpeg_get_session(my_obj, job_node->enc_info.job_id);
  if (NULL == p_session) {
    LOGE("invalid job id %x",
        job_node->enc_info.job_id);
    return -1;
  }

  LOGD("before dequeue session %d", ret);

  /* dequeue available omx handle */
  qdata = mm_jpeg_queue_deq(p_session->session_handle_q);
  p_session = qdata.p;

  if (NULL == p_session) {
    LOGH("No available sessions %d", ret);
    /* No available handles */
    qdata.p = job_node;
    mm_jpeg_queue_enq_head(&my_obj->job_mgr.job_queue, qdata);

    LOGH("end enqueue %d", ret);
    return rc;

  }

  p_session->auto_out_buf = OMX_FALSE;
  if (job_node->enc_info.encode_job.dst_index < 0) {
    /* dequeue available output buffer idx */
    qdata = mm_jpeg_queue_deq(p_session->out_buf_q);
    buf_idx = qdata.u32;

    if (0U == buf_idx) {
      LOGE("No available output buffers %d", ret);
      return OMX_ErrorUndefined;
    }

    buf_idx--;

    job_node->enc_info.encode_job.dst_index = (int32_t)buf_idx;
    p_session->auto_out_buf = OMX_TRUE;
  }

  /* sent encode cmd to OMX, queue job into ongoing queue */
  qdata.p = job_node;
  rc = mm_jpeg_queue_enq(&my_obj->ongoing_job_q, qdata);
  if (rc) {
    LOGE("jpeg enqueue failed %d", ret);
    goto error;
  }

  p_session->encode_job = job_node->enc_info.encode_job;
  p_session->jobId = job_node->enc_info.job_id;
  ret = mm_jpeg_session_encode(p_session);
  if (ret) {
    LOGE("encode session failed");
    goto error;
  }

  LOGH("Success X ");
  return rc;

error:

  if ((OMX_ErrorNone != ret) &&
    (NULL != p_session->params.jpeg_cb)) {
    p_session->job_status = JPEG_JOB_STATUS_ERROR;
    LOGE("send jpeg error callback %d",
      p_session->job_status);
    p_session->params.jpeg_cb(p_session->job_status,
      p_session->client_hdl,
      p_session->jobId,
      NULL,
      p_session->params.userdata);
  }

  /*remove the job*/
  mm_jpegenc_job_done(p_session);
  LOGD("Error X ");

  return rc;
}



/** mm_jpeg_jobmgr_thread:
 *
 *  Arguments:
 *    @my_obj: jpeg object
 *
 *  Return:
 *       0 for success else failure
 *
 *  Description:
 *       job manager thread main function
 *
 **/
static void *mm_jpeg_jobmgr_thread(void *data)
{
  mm_jpeg_q_data_t qdata;
  int rc = 0;
  int running = 1;
  uint32_t num_ongoing_jobs = 0;
  mm_jpeg_obj *my_obj = (mm_jpeg_obj*)data;
  mm_jpeg_job_cmd_thread_t *cmd_thread = &my_obj->job_mgr;
  mm_jpeg_job_q_node_t* node = NULL;
  prctl(PR_SET_NAME, (unsigned long)"mm_jpeg_thread", 0, 0, 0);

  do {
    do {
      rc = cam_sem_wait(&cmd_thread->job_sem);
      if (rc != 0 && errno != EINVAL) {
        LOGE("cam_sem_wait error (%s)",
           strerror(errno));
        return NULL;
      }
    } while (rc != 0);

    /* check ongoing q size */
    num_ongoing_jobs = mm_jpeg_queue_get_size(&my_obj->ongoing_job_q);

    LOGD("ongoing job  %d %d", num_ongoing_jobs, MM_JPEG_CONCURRENT_SESSIONS_COUNT);
    if (num_ongoing_jobs >= MM_JPEG_CONCURRENT_SESSIONS_COUNT) {
      LOGE("ongoing job already reach max %d", num_ongoing_jobs);
      continue;
    }

    pthread_mutex_lock(&my_obj->job_lock);
    /* can go ahead with new work */
    qdata = mm_jpeg_queue_deq(&cmd_thread->job_queue);
    node = (mm_jpeg_job_q_node_t*)qdata.p;
    if (node != NULL) {
      switch (node->type) {
      case MM_JPEG_CMD_TYPE_JOB:
        rc = mm_jpeg_process_encoding_job(my_obj, node);
        break;
      case MM_JPEG_CMD_TYPE_DECODE_JOB:
        rc = mm_jpegdec_process_decoding_job(my_obj, node);
        break;
      case MM_JPEG_CMD_TYPE_EXIT:
      default:
        /* free node */
        free(node);
        /* set running flag to false */
        running = 0;
        break;
      }
    }
    pthread_mutex_unlock(&my_obj->job_lock);

  } while (running);
  return NULL;
}

/** mm_jpeg_jobmgr_thread_launch:
 *
 *  Arguments:
 *    @my_obj: jpeg object
 *
 *  Return:
 *       0 for success else failure
 *
 *  Description:
 *       launches the job manager thread
 *
 **/
int32_t mm_jpeg_jobmgr_thread_launch(mm_jpeg_obj *my_obj)
{
  int32_t rc = 0;
  mm_jpeg_job_cmd_thread_t *job_mgr = &my_obj->job_mgr;

  cam_sem_init(&job_mgr->job_sem, 0);
  mm_jpeg_queue_init(&job_mgr->job_queue);

  /* launch the thread */
  pthread_create(&job_mgr->pid,
    NULL,
    mm_jpeg_jobmgr_thread,
    (void *)my_obj);
  pthread_setname_np(job_mgr->pid, "CAM_jpeg_jobmgr");
  return rc;
}

/** mm_jpeg_jobmgr_thread_release:
 *
 *  Arguments:
 *    @my_obj: jpeg object
 *
 *  Return:
 *       0 for success else failure
 *
 *  Description:
 *       Releases the job manager thread
 *
 **/
int32_t mm_jpeg_jobmgr_thread_release(mm_jpeg_obj * my_obj)
{
  mm_jpeg_q_data_t qdata;
  int32_t rc = 0;
  mm_jpeg_job_cmd_thread_t * cmd_thread = &my_obj->job_mgr;
  mm_jpeg_job_q_node_t* node =
    (mm_jpeg_job_q_node_t *)malloc(sizeof(mm_jpeg_job_q_node_t));
  if (NULL == node) {
    LOGE("No memory for mm_jpeg_job_q_node_t");
    return -1;
  }

  memset(node, 0, sizeof(mm_jpeg_job_q_node_t));
  node->type = MM_JPEG_CMD_TYPE_EXIT;

  qdata.p = node;
  mm_jpeg_queue_enq(&cmd_thread->job_queue, qdata);
  cam_sem_post(&cmd_thread->job_sem);

  /* wait until cmd thread exits */
  if (pthread_join(cmd_thread->pid, NULL) != 0) {
    LOGD("pthread dead already");
  }
  mm_jpeg_queue_deinit(&cmd_thread->job_queue);

  cam_sem_destroy(&cmd_thread->job_sem);
  memset(cmd_thread, 0, sizeof(mm_jpeg_job_cmd_thread_t));
  return rc;
}

/** mm_jpeg_alloc_workbuffer:
 *
 *  Arguments:
 *    @my_obj: jpeg object
 *    @work_bufs_need: number of work buffers required
 *    @work_buf_size: size of the work buffer
 *
 *  Return:
 *       greater or equal to 0 for success else failure
 *
 *  Description:
 *       Allocates work buffer
 *
 **/
int32_t mm_jpeg_alloc_workbuffer(mm_jpeg_obj *my_obj,
  uint32_t work_bufs_need,
  uint32_t work_buf_size)
{
  int32_t rc = 0;
  uint32_t i;
  LOGH("work_bufs_need %d work_buf_cnt %d",
    work_bufs_need, my_obj->work_buf_cnt);
  for (i = my_obj->work_buf_cnt; i < work_bufs_need; i++) {
    my_obj->ionBuffer[i].size = CEILING32(work_buf_size);
    LOGH("Max picture size %d x %d, WorkBufSize = %zu",
      my_obj->max_pic_w, my_obj->max_pic_h, my_obj->ionBuffer[i].size);
    my_obj->ionBuffer[i].addr = (uint8_t *)buffer_allocate(&my_obj->ionBuffer[i], 1);
    if (NULL == my_obj->ionBuffer[i].addr) {
      LOGE("Ion allocation failed");
      while (i--) {
        buffer_deallocate(&my_obj->ionBuffer[i]);
        my_obj->work_buf_cnt--;
      }
      return -1;
    }
    my_obj->work_buf_cnt++;
    rc = i;
  }
 LOGH("rc %d ", rc);
  return rc;
}

/** mm_jpeg_release_workbuffer:
 *
 *  Arguments:
 *    @my_obj: jpeg object
 *    @work_bufs_need: number of work buffers allocated
 *
 *  Return:
 *       0 for success else failure
 *
 *  Description:
 *       Releases the allocated work buffer
 *
 **/
int32_t mm_jpeg_release_workbuffer(mm_jpeg_obj *my_obj,
  uint32_t work_bufs_need)
{
  int32_t rc = 0;
  uint32_t i;
 LOGH("release work_bufs %d ", work_bufs_need);
  for (i = my_obj->work_buf_cnt; i < work_bufs_need; i++) {
    buffer_deallocate(&my_obj->ionBuffer[i]);
  }
  return rc;
}

/** mm_jpeg_init:
 *
 *  Arguments:
 *    @my_obj: jpeg object
 *
 *  Return:
 *       0 for success else failure
 *
 *  Description:
 *       Initializes the jpeg client
 *
 **/
int32_t mm_jpeg_init(mm_jpeg_obj *my_obj)
{
  int32_t rc = 0;
  uint32_t work_buf_size;
  unsigned int initial_workbufs_cnt = 1;

  /* init locks */
  pthread_mutex_init(&my_obj->job_lock, NULL);

  /* init ongoing job queue */
  rc = mm_jpeg_queue_init(&my_obj->ongoing_job_q);
  if (0 != rc) {
    LOGE("Error");
    pthread_mutex_destroy(&my_obj->job_lock);
    return -1;
  }


  /* init job semaphore and launch jobmgr thread */
  LOGD("Launch jobmgr thread rc %d", rc);
  rc = mm_jpeg_jobmgr_thread_launch(my_obj);
  if (0 != rc) {
    LOGE("Error");
    mm_jpeg_queue_deinit(&my_obj->ongoing_job_q);
    pthread_mutex_destroy(&my_obj->job_lock);
    return -1;
  }

  /* set work buf size from max picture size */
  if (my_obj->max_pic_w <= 0 || my_obj->max_pic_h <= 0) {
    LOGE("Width and height are not valid "
      "dimensions, cannot calc work buf size");
    mm_jpeg_jobmgr_thread_release(my_obj);
    mm_jpeg_queue_deinit(&my_obj->ongoing_job_q);
    pthread_mutex_destroy(&my_obj->job_lock);
    return -1;
  }

  /* allocate work buffer if reproc source buffer is not supposed to be used */
  if (!my_obj->reuse_reproc_buffer) {
    work_buf_size = CEILING64((uint32_t)my_obj->max_pic_w) *
     CEILING64((uint32_t)my_obj->max_pic_h) * 3U / 2U;
    rc = mm_jpeg_alloc_workbuffer(my_obj, initial_workbufs_cnt, work_buf_size);
    if (rc == -1) {
      LOGE("Work buffer allocation failure");
      return rc;
    }
  }

  /* load OMX */
  if (OMX_ErrorNone != OMX_Init()) {
    /* roll back in error case */
    LOGE("OMX_Init failed (%d)", rc);
    if (!my_obj->reuse_reproc_buffer) {
      mm_jpeg_release_workbuffer(my_obj, initial_workbufs_cnt);
    }
    mm_jpeg_jobmgr_thread_release(my_obj);
    mm_jpeg_queue_deinit(&my_obj->ongoing_job_q);
    pthread_mutex_destroy(&my_obj->job_lock);
  }

#ifdef LOAD_ADSP_RPC_LIB
  my_obj->adsprpc_lib_handle = dlopen("libadsprpc.so", RTLD_NOW);
  if (NULL == my_obj->adsprpc_lib_handle) {
    LOGE("Cannot load the library");
    /* not returning error here bcoz even if this loading fails
        we can go ahead with SW JPEG enc */
  }
#endif

  // create dummy OMX handle to avoid dlopen latency
  OMX_GetHandle(&my_obj->dummy_handle, mm_jpeg_get_comp_name(), NULL, NULL);

  return rc;
}

/** mm_jpeg_deinit:
 *
 *  Arguments:
 *    @my_obj: jpeg object
 *
 *  Return:
 *       0 for success else failure
 *
 *  Description:
 *       Deinits the jpeg client
 *
 **/
int32_t mm_jpeg_deinit(mm_jpeg_obj *my_obj)
{
  int32_t rc = 0;
  uint32_t i = 0;

  /* release jobmgr thread */
  rc = mm_jpeg_jobmgr_thread_release(my_obj);
  if (0 != rc) {
    LOGE("Error");
  }

  if (my_obj->dummy_handle) {
    OMX_FreeHandle(my_obj->dummy_handle);
  }

  /* unload OMX engine */
  OMX_Deinit();

  /* deinit ongoing job and cb queue */
  rc = mm_jpeg_queue_deinit(&my_obj->ongoing_job_q);
  if (0 != rc) {
    LOGE("Error");
  }

  for (i = 0; i < my_obj->work_buf_cnt; i++) {
    /*Release the ION buffer*/
    rc = buffer_deallocate(&my_obj->ionBuffer[i]);
    if (0 != rc) {
      LOGE("Error releasing ION buffer");
    }
  }
  my_obj->work_buf_cnt = 0;
  my_obj->jpeg_metadata = NULL;

  /* destroy locks */
  pthread_mutex_destroy(&my_obj->job_lock);

  return rc;
}

/** mm_jpeg_new_client:
 *
 *  Arguments:
 *    @my_obj: jpeg object
 *
 *  Return:
 *       0 for success else failure
 *
 *  Description:
 *       Create new jpeg client
 *
 **/
uint32_t mm_jpeg_new_client(mm_jpeg_obj *my_obj)
{
  uint32_t client_hdl = 0;
  uint8_t idx;
  int i = 0;

  if (my_obj->num_clients >= MAX_JPEG_CLIENT_NUM) {
    LOGE("num of clients reached limit");
    return client_hdl;
  }

  for (idx = 0; idx < MAX_JPEG_CLIENT_NUM; idx++) {
    if (0 == my_obj->clnt_mgr[idx].is_used) {
      break;
    }
  }

  if (idx < MAX_JPEG_CLIENT_NUM) {
    /* client session avail */
    /* generate client handler by index */
    client_hdl = mm_jpeg_util_generate_handler(idx);

    /* update client session */
    my_obj->clnt_mgr[idx].is_used = 1;
    my_obj->clnt_mgr[idx].client_handle = client_hdl;

    pthread_mutex_init(&my_obj->clnt_mgr[idx].lock, NULL);
    for (i = 0; i < MM_JPEG_MAX_SESSION; i++) {
      memset(&my_obj->clnt_mgr[idx].session[i], 0x0, sizeof(mm_jpeg_job_session_t));
    }

    /* increse client count */
    my_obj->num_clients++;
  }

  return client_hdl;
}

#ifdef LIB2D_ROTATION_ENABLE
/**
 * Function: mm_jpeg_lib2d_rotation_cb
 *
 * Description: Callback that is called on completion of requested job.
 *
 * Input parameters:
 *   userdata - App userdata
 *   jobid - job id that is finished execution
 *
 * Return values:
 *   MM_LIB2D_SUCCESS
 *   MM_LIB2D_ERR_GENERAL
 *
 * Notes: none
 **/
lib2d_error mm_jpeg_lib2d_rotation_cb(void *userdata, int jobid)
{
  LOGD("Received CB from lib2d\n");
  return MM_LIB2D_SUCCESS;
}

/**
 * Function: mm_jpeg_lib2d_rotation
 *
 * Description: lib2d rotation function.
 *
 * Input parameters:
 *   p_session - pointer to session
 *   p_node - pointer to job queue node
 *   p_job - pointer to job
 *   p_job_id - pointer to job id
 *
 * Return values:
 *   0 - success
 *   -1 - failure
 *
 * Notes: none
 **/
int32_t mm_jpeg_lib2d_rotation(mm_jpeg_job_session_t *p_session,
  mm_jpeg_job_q_node_t* p_node, mm_jpeg_job_t *p_job, uint32_t *p_job_id)
{
  lib2d_error lib2d_err = MM_LIB2D_SUCCESS;
  mm_lib2d_buffer src_buffer;
  mm_lib2d_buffer dst_buffer;
  mm_jpeg_buf_t *p_src_main_buf = p_session->params.src_main_buf;
  mm_jpeg_buf_t *p_src_rot_main_buf = p_session->src_rot_main_buf;
  mm_jpeg_encode_job_t *p_jobparams  = &p_job->encode_job;
  mm_jpeg_encode_job_t *p_jobparams_node = &p_node->enc_info.encode_job;
  cam_format_t format;
  int32_t scanline = 0;

  memset(&src_buffer, 0x0, sizeof(mm_lib2d_buffer));
  memset(&dst_buffer, 0x0, sizeof(mm_lib2d_buffer));

  switch (p_session->params.rotation) {
  case 0:
    break;
  case 90:
    p_jobparams_node->main_dim.src_dim.width =
      p_jobparams->main_dim.src_dim.height;
    p_jobparams_node->main_dim.src_dim.height =
      p_jobparams->main_dim.src_dim.width;

    p_jobparams_node->main_dim.dst_dim.width =
      p_jobparams->main_dim.dst_dim.height;
    p_jobparams_node->main_dim.dst_dim.height =
      p_jobparams->main_dim.dst_dim.width;

    p_jobparams_node->main_dim.crop.width =
      p_jobparams->main_dim.crop.height;
    p_jobparams_node->main_dim.crop.height =
      p_jobparams->main_dim.crop.width;

    if (p_jobparams->main_dim.crop.top ||
      p_jobparams->main_dim.crop.height) {
      p_jobparams_node->main_dim.crop.left =
        p_jobparams->main_dim.src_dim.height -
        (p_jobparams->main_dim.crop.top +
        p_jobparams->main_dim.crop.height);
    } else {
      p_jobparams_node->main_dim.crop.left = 0;
    }
    p_jobparams_node->main_dim.crop.top =
      p_jobparams->main_dim.crop.left;
    break;
  case 180:
    if (p_jobparams->main_dim.crop.left ||
      p_jobparams->main_dim.crop.width) {
      p_jobparams_node->main_dim.crop.left =
        p_jobparams->main_dim.src_dim.width -
        (p_jobparams->main_dim.crop.left +
        p_jobparams->main_dim.crop.width);
    } else {
      p_jobparams_node->main_dim.crop.left = 0;
    }

    if (p_jobparams->main_dim.crop.top ||
      p_jobparams->main_dim.crop.height) {
      p_jobparams_node->main_dim.crop.top =
        p_jobparams->main_dim.src_dim.height -
        (p_jobparams->main_dim.crop.top +
        p_jobparams->main_dim.crop.height);
    } else {
      p_jobparams_node->main_dim.crop.top = 0;
    }
    break;
  case 270:
    p_jobparams_node->main_dim.src_dim.width =
      p_jobparams->main_dim.src_dim.height;
    p_jobparams_node->main_dim.src_dim.height =
      p_jobparams->main_dim.src_dim.width;

    p_jobparams_node->main_dim.dst_dim.width =
      p_jobparams->main_dim.dst_dim.height;
    p_jobparams_node->main_dim.dst_dim.height =
      p_jobparams->main_dim.dst_dim.width;

    p_jobparams_node->main_dim.crop.width =
      p_jobparams->main_dim.crop.height;
    p_jobparams_node->main_dim.crop.height =
      p_jobparams->main_dim.crop.width;
    p_jobparams_node->main_dim.crop.left =
      p_jobparams->main_dim.crop.top;
    if (p_jobparams->main_dim.crop.left ||
      p_jobparams->main_dim.crop.width) {
      p_jobparams_node->main_dim.crop.top =
        p_jobparams->main_dim.src_dim.width -
        (p_jobparams->main_dim.crop.left +
        p_jobparams->main_dim.crop.width);
    } else {
      p_jobparams_node->main_dim.crop.top = 0;
    }
    break;
  }

  LOGD("crop wxh %dx%d txl %dx%d",
    p_jobparams_node->main_dim.crop.width,
    p_jobparams_node->main_dim.crop.height,
    p_jobparams_node->main_dim.crop.top,
    p_jobparams_node->main_dim.crop.left);

  format = mm_jpeg_get_imgfmt_from_colorfmt(p_session->params.color_format);
  src_buffer.buffer_type = MM_LIB2D_BUFFER_TYPE_YUV;
  src_buffer.yuv_buffer.fd =
    p_src_main_buf[p_jobparams->src_index].fd;
  src_buffer.yuv_buffer.format = format;
  src_buffer.yuv_buffer.width = p_jobparams->main_dim.src_dim.width;
  src_buffer.yuv_buffer.height = p_jobparams->main_dim.src_dim.height;
  src_buffer.yuv_buffer.plane0 =
    p_src_main_buf[p_jobparams->src_index].buf_vaddr;
  src_buffer.yuv_buffer.stride0 =
    p_src_main_buf[p_jobparams->src_index].offset.mp[0].stride;
  scanline = p_src_main_buf[p_jobparams->src_index].offset.mp[0].scanline;
  src_buffer.yuv_buffer.plane1 =
    (uint8_t*)src_buffer.yuv_buffer.plane0 +
    (src_buffer.yuv_buffer.stride0 * scanline);
  src_buffer.yuv_buffer.stride1 = src_buffer.yuv_buffer.stride0;

  LOGD(" lib2d SRC wxh = %dx%d , stxsl = %dx%d\n",
    src_buffer.yuv_buffer.width, src_buffer.yuv_buffer.height,
    src_buffer.yuv_buffer.stride0, scanline);

  dst_buffer.buffer_type = MM_LIB2D_BUFFER_TYPE_YUV;
  dst_buffer.yuv_buffer.fd =
    p_src_rot_main_buf[p_jobparams->src_index].fd;
  dst_buffer.yuv_buffer.format = format;
  dst_buffer.yuv_buffer.width = p_jobparams_node->main_dim.src_dim.width;
  dst_buffer.yuv_buffer.height = p_jobparams_node->main_dim.src_dim.height;
  dst_buffer.yuv_buffer.plane0 =
    p_src_rot_main_buf[p_jobparams->src_index].buf_vaddr;

  if ((p_session->params.rotation == 90) ||
    (p_session->params.rotation == 270)) {
    dst_buffer.yuv_buffer.stride0 =
      p_src_main_buf[p_jobparams->src_index].offset.mp[0].scanline;
    scanline = p_src_main_buf[p_jobparams->src_index].offset.mp[0].stride;
  } else {
    dst_buffer.yuv_buffer.stride0 =
      p_src_main_buf[p_jobparams->src_index].offset.mp[0].stride;
    scanline = p_src_main_buf[p_jobparams->src_index].offset.mp[0].scanline;
  }

  dst_buffer.yuv_buffer.plane1 =
    (uint8_t*) dst_buffer.yuv_buffer.plane0 +
    (dst_buffer.yuv_buffer.stride0 * scanline);
  dst_buffer.yuv_buffer.stride1 = dst_buffer.yuv_buffer.stride0;

  LOGD(" lib2d DEST wxh = %dx%d , stxsl = %dx%d\n",
    dst_buffer.yuv_buffer.width, dst_buffer.yuv_buffer.height,
    dst_buffer.yuv_buffer.stride0, scanline);

  LOGD(" lib2d rotation = %d\n", p_session->params.rotation);

  lib2d_err = mm_lib2d_start_job(p_session->lib2d_handle, &src_buffer,
    &dst_buffer, *p_job_id, NULL, mm_jpeg_lib2d_rotation_cb,
    p_session->params.rotation);
  if (lib2d_err != MM_LIB2D_SUCCESS) {
    LOGE("Error in mm_lib2d_start_job \n");
    return -1;
  }

  buffer_clean(&p_session->src_rot_ion_buffer[p_jobparams->src_index]);

  return 0;
}
#endif

/** mm_jpeg_start_job:
 *
 *  Arguments:
 *    @my_obj: jpeg object
 *    @client_hdl: client handle
 *    @job: pointer to encode job
 *    @jobId: job id
 *
 *  Return:
 *       0 for success else failure
 *
 *  Description:
 *       Start the encoding job
 *
 **/
int32_t mm_jpeg_start_job(mm_jpeg_obj *my_obj,
  mm_jpeg_job_t *job,
  uint32_t *job_id)
{
  mm_jpeg_q_data_t qdata;
  int32_t rc = -1;
  uint8_t session_idx = 0;
  uint8_t client_idx = 0;
  mm_jpeg_job_q_node_t* node = NULL;
  mm_jpeg_job_session_t *p_session = NULL;
  mm_jpeg_encode_job_t *p_jobparams  = NULL;
  uint32_t work_bufs_need;
  uint32_t work_buf_size;

  *job_id = 0;

  if (!job) {
    LOGE("invalid job !!!");
    return rc;
  }
  p_jobparams = &job->encode_job;

  /* check if valid session */
  session_idx = GET_SESSION_IDX(p_jobparams->session_id);
  client_idx = GET_CLIENT_IDX(p_jobparams->session_id);
  LOGD("session_idx %d client idx %d",
    session_idx, client_idx);

  if ((session_idx >= MM_JPEG_MAX_SESSION) ||
    (client_idx >= MAX_JPEG_CLIENT_NUM)) {
    LOGE("invalid session id %x",
      job->encode_job.session_id);
    return rc;
  }

  p_session = &my_obj->clnt_mgr[client_idx].session[session_idx];

  if (my_obj->reuse_reproc_buffer) {
    p_session->work_buffer.addr           = p_jobparams->work_buf.buf_vaddr;
    p_session->work_buffer.size           = p_jobparams->work_buf.buf_size;
    p_session->work_buffer.ion_info_fd.fd = p_jobparams->work_buf.fd;
    p_session->work_buffer.p_pmem_fd      = p_jobparams->work_buf.fd;

    work_bufs_need = my_obj->num_sessions + 1;
    if (work_bufs_need > MM_JPEG_CONCURRENT_SESSIONS_COUNT) {
      work_bufs_need = MM_JPEG_CONCURRENT_SESSIONS_COUNT;
    }

    if (p_session->work_buffer.addr) {
      work_bufs_need--;
      LOGD("HAL passed the work buffer of size = %d; don't alloc internally",
          p_session->work_buffer.size);
    } else {
      p_session->work_buffer = my_obj->ionBuffer[0];
    }

    LOGD(">>>> Work bufs need %d, %d",
      work_bufs_need, my_obj->work_buf_cnt);
    if (work_bufs_need) {
      work_buf_size = CEILING64(my_obj->max_pic_w) *
        CEILING64(my_obj->max_pic_h) * 3 / 2;
      rc = mm_jpeg_alloc_workbuffer(my_obj, work_bufs_need, work_buf_size);
      if (rc == -1) {
        LOGE("Work buffer allocation failure");
        return rc;
      } else {
        p_session->work_buffer = my_obj->ionBuffer[rc];
      }
    }
  }

  if (OMX_FALSE == p_session->active) {
    LOGE("session not active %x",
      job->encode_job.session_id);
    return rc;
  }

  if ((p_jobparams->src_index >= (int32_t)p_session->params.num_src_bufs) ||
    (p_jobparams->dst_index >= (int32_t)p_session->params.num_dst_bufs)) {
    LOGE("invalid buffer indices");
    return rc;
  }

  /* enqueue new job into todo job queue */
  node = (mm_jpeg_job_q_node_t *)malloc(sizeof(mm_jpeg_job_q_node_t));
  if (NULL == node) {
    LOGE("No memory for mm_jpeg_job_q_node_t");
    return -1;
  }

  KPI_ATRACE_INT("Camera:JPEG",
      (int32_t)((uint32_t)session_idx<<16 | ++p_session->job_index));

  *job_id = job->encode_job.session_id |
    (((uint32_t)p_session->job_hist++ % JOB_HIST_MAX) << 16);

  memset(node, 0, sizeof(mm_jpeg_job_q_node_t));
  node->enc_info.encode_job = job->encode_job;

#ifdef LIB2D_ROTATION_ENABLE
  if (p_session->lib2d_rotation_flag) {
    rc = mm_jpeg_lib2d_rotation(p_session, node, job, job_id);
    if (rc < 0) {
      LOGE("Lib2d rotation failed");
      return rc;
    }
  }
#endif

  if (p_session->thumb_from_main) {
    node->enc_info.encode_job.thumb_dim.src_dim =
      node->enc_info.encode_job.main_dim.src_dim;
    node->enc_info.encode_job.thumb_dim.crop =
      node->enc_info.encode_job.main_dim.crop;
    if (p_session->lib2d_rotation_flag) {
      if ((p_session->params.rotation == 90) ||
        (p_session->params.rotation == 270)) {
        node->enc_info.encode_job.thumb_dim.dst_dim.width =
          job->encode_job.thumb_dim.dst_dim.height;
        node->enc_info.encode_job.thumb_dim.dst_dim.height =
          job->encode_job.thumb_dim.dst_dim.width;
      }
    }
  }
  node->enc_info.job_id = *job_id;
  node->enc_info.client_handle = p_session->client_hdl;
  node->type = MM_JPEG_CMD_TYPE_JOB;

  qdata.p = node;
  rc = mm_jpeg_queue_enq(&my_obj->job_mgr.job_queue, qdata);
  if (0 == rc) {
      cam_sem_post(&my_obj->job_mgr.job_sem);
  }

  LOGH("session_idx %u client_idx %u job_id %d X",
    session_idx, client_idx, *job_id);

  return rc;
}



/** mm_jpeg_abort_job:
 *
 *  Arguments:
 *    @my_obj: jpeg object
 *    @client_hdl: client handle
 *    @jobId: job id
 *
 *  Return:
 *       0 for success else failure
 *
 *  Description:
 *       Abort the encoding session
 *
 **/
int32_t mm_jpeg_abort_job(mm_jpeg_obj *my_obj,
  uint32_t jobId)
{
  int32_t rc = -1;
  mm_jpeg_job_q_node_t *node = NULL;
  mm_jpeg_job_session_t *p_session = NULL;

  pthread_mutex_lock(&my_obj->job_lock);

  /* abort job if in todo queue */
  node = mm_jpeg_queue_remove_job_by_job_id(&my_obj->job_mgr.job_queue, jobId);
  if (NULL != node) {
    free(node);
    goto abort_done;
  }

  /* abort job if in ongoing queue */
  node = mm_jpeg_queue_remove_job_by_job_id(&my_obj->ongoing_job_q, jobId);
  if (NULL != node) {
    /* find job that is OMX ongoing, ask OMX to abort the job */
    p_session = mm_jpeg_get_session(my_obj, node->enc_info.job_id);
    if (p_session) {
      mm_jpeg_session_abort(p_session);
    } else {
      LOGE("Invalid job id 0x%x",
        node->enc_info.job_id);
    }
    free(node);
    goto abort_done;
  }

abort_done:
  pthread_mutex_unlock(&my_obj->job_lock);

  return rc;
}


#ifdef MM_JPEG_READ_META_KEYFILE
static int32_t mm_jpeg_read_meta_keyfile(mm_jpeg_job_session_t *p_session,
    const char *filename)
{
  int rc = 0;
  FILE *fp = NULL;
  size_t file_size = 0;
  fp = fopen(filename, "r");
  if (!fp) {
    LOGE("Key not present");
    return -1;
  }
  fseek(fp, 0, SEEK_END);
  file_size = (size_t)ftell(fp);
  fseek(fp, 0, SEEK_SET);

  p_session->meta_enc_key = (uint8_t *) malloc((file_size + 1) * sizeof(uint8_t));

  if (!p_session->meta_enc_key) {
    LOGE("error");
    return -1;
  }

  fread(p_session->meta_enc_key, 1, file_size, fp);
  fclose(fp);

  p_session->meta_enc_keylen = file_size;

  return rc;
}
#endif // MM_JPEG_READ_META_KEYFILE

/** mm_jpeg_create_session:
 *
 *  Arguments:
 *    @my_obj: jpeg object
 *    @client_hdl: client handle
 *    @p_params: pointer to encode params
 *    @p_session_id: session id
 *
 *  Return:
 *       0 for success else failure
 *
 *  Description:
 *       Start the encoding session
 *
 **/
int32_t mm_jpeg_create_session(mm_jpeg_obj *my_obj,
  uint32_t client_hdl,
  mm_jpeg_encode_params_t *p_params,
  uint32_t* p_session_id)
{
  mm_jpeg_q_data_t qdata;
  int32_t rc = 0;
  OMX_ERRORTYPE ret = OMX_ErrorNone;
  uint8_t clnt_idx = 0;
  int session_idx = -1;
  mm_jpeg_job_session_t *p_session = NULL;
  mm_jpeg_job_session_t * p_prev_session = NULL;
  *p_session_id = 0;
  uint32_t i = 0;
  uint32_t j = 0;
  uint32_t num_omx_sessions = 1;
  uint32_t work_buf_size;
  mm_jpeg_queue_t *p_session_handle_q, *p_out_buf_q;
  uint32_t work_bufs_need;
  char trace_tag[32];

  /* validate the parameters */
  if ((p_params->num_src_bufs > MM_JPEG_MAX_BUF)
    || (p_params->num_dst_bufs > MM_JPEG_MAX_BUF)) {
    LOGE("invalid num buffers");
    return -1;
  }

  /* check if valid client */
  clnt_idx = mm_jpeg_util_get_index_by_handler(client_hdl);
  if (clnt_idx >= MAX_JPEG_CLIENT_NUM) {
    LOGE("invalid client with handler (%d)", client_hdl);
    return -1;
  }

  if (p_params->burst_mode) {
    num_omx_sessions = MM_JPEG_CONCURRENT_SESSIONS_COUNT;
  }

  if (!my_obj->reuse_reproc_buffer) {
    work_bufs_need = num_omx_sessions;
    if (work_bufs_need > MM_JPEG_CONCURRENT_SESSIONS_COUNT) {
      work_bufs_need = MM_JPEG_CONCURRENT_SESSIONS_COUNT;
    }
    LOGD(">>>> Work bufs need %d", work_bufs_need);
    work_buf_size = CEILING64(my_obj->max_pic_w) *
      CEILING64(my_obj->max_pic_h) * 3 / 2;
    rc = mm_jpeg_alloc_workbuffer(my_obj, work_bufs_need, work_buf_size);
    if (rc == -1) {
      LOGE("Work buffer allocation failure");
      return rc;
    }
  }


  /* init omx handle queue */
  p_session_handle_q = (mm_jpeg_queue_t *) malloc(sizeof(*p_session_handle_q));
  if (NULL == p_session_handle_q) {
    LOGE("Error");
    goto error1;
  }
  rc = mm_jpeg_queue_init(p_session_handle_q);
  if (0 != rc) {
    LOGE("Error");
    free(p_session_handle_q);
    goto error1;
  }

  /* init output buf queue */
  p_out_buf_q = (mm_jpeg_queue_t *) malloc(sizeof(*p_out_buf_q));
  if (NULL == p_out_buf_q) {
    LOGE("Error: Cannot allocate memory\n");
    return -1;
  }

  /* init omx handle queue */
  rc = mm_jpeg_queue_init(p_out_buf_q);
  if (0 != rc) {
    LOGE("Error");
    free(p_out_buf_q);
    goto error1;
  }

  for (i = 0; i < num_omx_sessions; i++) {
    uint32_t buf_idx = 0U;
    session_idx = mm_jpeg_get_new_session_idx(my_obj, clnt_idx, &p_session);
    if (session_idx < 0 || NULL == p_session) {
      LOGE("invalid session id (%d)", session_idx);
      goto error2;
    }

    snprintf(trace_tag, sizeof(trace_tag), "Camera:JPEGsession%d", session_idx);
    ATRACE_INT(trace_tag, 1);

    p_session->job_index = 0;

    p_session->next_session = NULL;

    if (p_prev_session) {
      p_prev_session->next_session = p_session;
    }
    p_prev_session = p_session;

    buf_idx = i;
    if (buf_idx < MM_JPEG_CONCURRENT_SESSIONS_COUNT) {
      p_session->work_buffer = my_obj->ionBuffer[buf_idx];
    } else {
      LOGE("Invalid Index, Setting buffer add to null");
      p_session->work_buffer.addr = NULL;
      p_session->work_buffer.ion_fd = -1;
      p_session->work_buffer.p_pmem_fd = -1;
    }

    p_session->jpeg_obj = (void*)my_obj; /* save a ptr to jpeg_obj */

    /*copy the params*/
    p_session->params = *p_params;
    ret = mm_jpeg_session_create(p_session);
    if (OMX_ErrorNone != ret) {
      p_session->active = OMX_FALSE;
      LOGE("jpeg session create failed");
      goto error2;
    }

    uint32_t session_id = (JOB_ID_MAGICVAL << 24) |
        ((uint32_t)session_idx << 8) | clnt_idx;

    if (!*p_session_id) {
      *p_session_id = session_id;
    }

    if (p_session->thumb_from_main) {
      memcpy(p_session->params.src_thumb_buf, p_session->params.src_main_buf,
        sizeof(p_session->params.src_thumb_buf));
      p_session->params.num_tmb_bufs =  p_session->params.num_src_bufs;
      if (!p_session->params.encode_thumbnail) {
         p_session->params.num_tmb_bufs = 0;
      }
      p_session->params.thumb_dim.src_dim = p_session->params.main_dim.src_dim;
      p_session->params.thumb_dim.crop = p_session->params.main_dim.crop;
    }
#ifdef LIB2D_ROTATION_ENABLE
    if (p_session->params.rotation) {
      LOGD("Enable lib2d rotation");
      p_session->lib2d_rotation_flag = 1;

      cam_format_t lib2d_format;
      lib2d_error lib2d_err = MM_LIB2D_SUCCESS;
      lib2d_format =
        mm_jpeg_get_imgfmt_from_colorfmt(p_session->params.color_format);
      lib2d_err = mm_lib2d_init(MM_LIB2D_SYNC_MODE, lib2d_format,
      lib2d_format, &p_session->lib2d_handle);
      if (lib2d_err != MM_LIB2D_SUCCESS) {
        LOGE("lib2d init for rotation failed\n");
        rc = -1;
        p_session->lib2d_rotation_flag = 0;
        goto error2;
      }
    } else {
      LOGD("Disable lib2d rotation");
      p_session->lib2d_rotation_flag = 0;
    }
#else
    p_session->lib2d_rotation_flag = 0;
#endif

    if (p_session->lib2d_rotation_flag) {
      p_session->num_src_rot_bufs = p_session->params.num_src_bufs;
      memset(p_session->src_rot_main_buf, 0,
        sizeof(p_session->src_rot_main_buf));

      for (j = 0; j < p_session->num_src_rot_bufs; j++) {
        p_session->src_rot_main_buf[j].buf_size =
          p_session->params.src_main_buf[j].buf_size;
        p_session->src_rot_main_buf[j].format =
          p_session->params.src_main_buf[j].format;
        p_session->src_rot_main_buf[j].index = j;

        memset(&p_session->src_rot_ion_buffer[j], 0, sizeof(buffer_t));
        p_session->src_rot_ion_buffer[j].size =
          p_session->src_rot_main_buf[j].buf_size;
        p_session->src_rot_ion_buffer[j].addr =
          (uint8_t *)buffer_allocate(&p_session->src_rot_ion_buffer[j], 1);

        if (NULL == p_session->src_rot_ion_buffer[j].addr) {
          LOGE("Ion buff alloc for rotation failed");
          // deallocate all previously allocated rotation ion buffs
          for (j = 0; j < p_session->num_src_rot_bufs; j++) {
            if (p_session->src_rot_ion_buffer[j].addr) {
              buffer_deallocate(&p_session->src_rot_ion_buffer[j]);
            }
          }
          //fall back to SW encoding for rotation
          p_session->lib2d_rotation_flag = 0;
        } else {
          p_session->src_rot_main_buf[j].buf_vaddr =
            p_session->src_rot_ion_buffer[j].addr;
          p_session->src_rot_main_buf[j].fd =
            p_session->src_rot_ion_buffer[j].p_pmem_fd;
        }
      }
    }

    p_session->client_hdl = client_hdl;
    p_session->sessionId = session_id;
    p_session->session_handle_q = p_session_handle_q;
    p_session->out_buf_q = p_out_buf_q;

    qdata.p = p_session;
    mm_jpeg_queue_enq(p_session_handle_q, qdata);

    p_session->meta_enc_key = NULL;
    p_session->meta_enc_keylen = 0;

#ifdef MM_JPEG_READ_META_KEYFILE
    mm_jpeg_read_meta_keyfile(p_session, META_KEYFILE);
#endif

    pthread_mutex_lock(&my_obj->job_lock);
    /* Configure session if not already configured and if
       no other session configured*/
    if ((OMX_FALSE == p_session->config) &&
      (my_obj->p_session_inprogress == NULL)) {
      rc = mm_jpeg_session_configure(p_session);
      if (rc) {
        LOGE("Error");
        pthread_mutex_unlock(&my_obj->job_lock);
        goto error2;
      }
      p_session->config = OMX_TRUE;
      my_obj->p_session_inprogress = p_session;
    }
    pthread_mutex_unlock(&my_obj->job_lock);
    p_session->num_omx_sessions = num_omx_sessions;

    LOGH("session id %x thumb_from_main %d",
      session_id, p_session->thumb_from_main);
  }

  // Queue the output buf indexes
  for (i = 0; i < p_params->num_dst_bufs; i++) {
    qdata.u32 = i + 1;
    mm_jpeg_queue_enq(p_out_buf_q, qdata);
  }

  return rc;

error1:
  rc = -1;
error2:
  if (NULL != p_session) {
    ATRACE_INT(trace_tag, 0);
  }
  return rc;
}

/** mm_jpegenc_destroy_job
 *
 *  Arguments:
 *    @p_session: Session obj
 *
 *  Return:
 *       0 for success else failure
 *
 *  Description:
 *       Destroy the job based paramenters
 *
 **/
static int32_t mm_jpegenc_destroy_job(mm_jpeg_job_session_t *p_session)
{
  mm_jpeg_encode_job_t *p_jobparams = &p_session->encode_job;
  int i = 0, rc = 0;

  LOGD("Exif entry count %d %d",
    (int)p_jobparams->exif_info.numOfEntries,
    (int)p_session->exif_count_local);
  for (i = 0; i < p_session->exif_count_local; i++) {
    rc = releaseExifEntry(&p_session->exif_info_local[i]);
    if (rc) {
      LOGE("Exif release failed (%d)", rc);
    }
  }
  p_session->exif_count_local = 0;

  return rc;
}

/** mm_jpeg_session_encode:
 *
 *  Arguments:
 *    @p_session: encode session
 *
 *  Return:
 *       OMX_ERRORTYPE
 *
 *  Description:
 *       Start the encoding
 *
 **/
static void mm_jpegenc_job_done(mm_jpeg_job_session_t *p_session)
{
  mm_jpeg_q_data_t qdata;
  mm_jpeg_obj *my_obj = (mm_jpeg_obj *)p_session->jpeg_obj;
  mm_jpeg_job_q_node_t *node = NULL;

  /*Destroy job related params*/
  mm_jpegenc_destroy_job(p_session);

  /*remove the job*/
  node = mm_jpeg_queue_remove_job_by_job_id(&my_obj->ongoing_job_q,
    p_session->jobId);
  if (node) {
    free(node);
  }
  p_session->encoding = OMX_FALSE;

  // Queue to available sessions
  qdata.p = p_session;
  mm_jpeg_queue_enq(p_session->session_handle_q, qdata);

  if (p_session->auto_out_buf) {
    //Queue out buf index
    qdata.u32 = (uint32_t)(p_session->encode_job.dst_index + 1);
    mm_jpeg_queue_enq(p_session->out_buf_q, qdata);
  }

  /* wake up jobMgr thread to work on new job if there is any */
  cam_sem_post(&my_obj->job_mgr.job_sem);
}

/** mm_jpeg_destroy_session:
 *
 *  Arguments:
 *    @my_obj: jpeg object
 *    @session_id: session index
 *
 *  Return:
 *       0 for success else failure
 *
 *  Description:
 *       Destroy the encoding session
 *
 **/
int32_t mm_jpeg_destroy_session(mm_jpeg_obj *my_obj,
  mm_jpeg_job_session_t *p_session)
{
  mm_jpeg_q_data_t qdata;
  int32_t rc = 0;
  mm_jpeg_job_q_node_t *node = NULL;
  uint32_t session_id = 0;
  mm_jpeg_job_session_t *p_cur_sess;
  char trace_tag[32];

  if (NULL == p_session) {
    LOGE("invalid session");
    return rc;
  }

  session_id = p_session->sessionId;

  pthread_mutex_lock(&my_obj->job_lock);

  /* abort job if in todo queue */
  LOGD("abort todo jobs");
  node = mm_jpeg_queue_remove_job_by_session_id(&my_obj->job_mgr.job_queue, session_id);
  while (NULL != node) {
    free(node);
    node = mm_jpeg_queue_remove_job_by_session_id(&my_obj->job_mgr.job_queue, session_id);
  }

  /* abort job if in ongoing queue */
  LOGD("abort ongoing jobs");
  node = mm_jpeg_queue_remove_job_by_session_id(&my_obj->ongoing_job_q, session_id);
  while (NULL != node) {
    free(node);
    node = mm_jpeg_queue_remove_job_by_session_id(&my_obj->ongoing_job_q, session_id);
  }

  /* abort the current session */
  mm_jpeg_session_abort(p_session);

#ifdef LIB2D_ROTATION_ENABLE
  lib2d_error lib2d_err = MM_LIB2D_SUCCESS;
  if (p_session->lib2d_rotation_flag) {
    lib2d_err = mm_lib2d_deinit(p_session->lib2d_handle);
    if (lib2d_err != MM_LIB2D_SUCCESS) {
      LOGE("Error in mm_lib2d_deinit \n");
    }
  }
#endif

  mm_jpeg_session_destroy(p_session);

  p_cur_sess = p_session;

  do {
    mm_jpeg_remove_session_idx(my_obj, p_cur_sess->sessionId);
  } while (NULL != (p_cur_sess = p_cur_sess->next_session));


  pthread_mutex_unlock(&my_obj->job_lock);

  while (1) {
    qdata = mm_jpeg_queue_deq(p_session->session_handle_q);
    if (NULL == qdata.p)
      break;
  }
  mm_jpeg_queue_deinit(p_session->session_handle_q);
  free(p_session->session_handle_q);
  p_session->session_handle_q = NULL;

  while (1) {
    qdata = mm_jpeg_queue_deq(p_session->out_buf_q);
    if (0U == qdata.u32)
      break;
  }
  mm_jpeg_queue_deinit(p_session->out_buf_q);
  free(p_session->out_buf_q);
  p_session->out_buf_q = NULL;


  /* wake up jobMgr thread to work on new job if there is any */
  cam_sem_post(&my_obj->job_mgr.job_sem);

  snprintf(trace_tag, sizeof(trace_tag), "Camera:JPEGsession%d", GET_SESSION_IDX(session_id));
  ATRACE_INT(trace_tag, 0);

  LOGH("destroy session successful. X");

  return rc;
}




/** mm_jpeg_destroy_session:
 *
 *  Arguments:
 *    @my_obj: jpeg object
 *    @session_id: session index
 *
 *  Return:
 *       0 for success else failure
 *
 *  Description:
 *       Destroy the encoding session
 *
 **/
int32_t mm_jpeg_destroy_session_unlocked(mm_jpeg_obj *my_obj,
  mm_jpeg_job_session_t *p_session)
{
  int32_t rc = -1;
  mm_jpeg_job_q_node_t *node = NULL;
  uint32_t session_id = 0;
  if (NULL == p_session) {
    LOGE("invalid session");
    return rc;
  }

  session_id = p_session->sessionId;

  /* abort job if in todo queue */
  LOGD("abort todo jobs");
  node = mm_jpeg_queue_remove_job_by_session_id(&my_obj->job_mgr.job_queue, session_id);
  while (NULL != node) {
    free(node);
    node = mm_jpeg_queue_remove_job_by_session_id(&my_obj->job_mgr.job_queue, session_id);
  }

  /* abort job if in ongoing queue */
  LOGD("abort ongoing jobs");
  node = mm_jpeg_queue_remove_job_by_session_id(&my_obj->ongoing_job_q, session_id);
  while (NULL != node) {
    free(node);
    node = mm_jpeg_queue_remove_job_by_session_id(&my_obj->ongoing_job_q, session_id);
  }

  /* abort the current session */
  mm_jpeg_session_abort(p_session);
  //mm_jpeg_remove_session_idx(my_obj, session_id);

  return rc;
}

/** mm_jpeg_destroy_session:
 *
 *  Arguments:
 *    @my_obj: jpeg object
 *    @session_id: session index
 *
 *  Return:
 *       0 for success else failure
 *
 *  Description:
 *       Destroy the encoding session
 *
 **/
int32_t mm_jpeg_destroy_session_by_id(mm_jpeg_obj *my_obj, uint32_t session_id)
{
  mm_jpeg_job_session_t *p_session = mm_jpeg_get_session(my_obj, session_id);

  return mm_jpeg_destroy_session(my_obj, p_session);
}



/** mm_jpeg_close:
 *
 *  Arguments:
 *    @my_obj: jpeg object
 *    @client_hdl: client handle
 *
 *  Return:
 *       0 for success else failure
 *
 *  Description:
 *       Close the jpeg client
 *
 **/
int32_t mm_jpeg_close(mm_jpeg_obj *my_obj, uint32_t client_hdl)
{
  int32_t rc = -1;
  uint8_t clnt_idx = 0;
  int i = 0;

  /* check if valid client */
  clnt_idx = mm_jpeg_util_get_index_by_handler(client_hdl);
  if (clnt_idx >= MAX_JPEG_CLIENT_NUM) {
    LOGE("invalid client with handler (%d)", client_hdl);
    return rc;
  }

  LOGD("E");

  /* abort all jobs from the client */
  pthread_mutex_lock(&my_obj->job_lock);

  for (i = 0; i < MM_JPEG_MAX_SESSION; i++) {
    if (OMX_TRUE == my_obj->clnt_mgr[clnt_idx].session[i].active)
      mm_jpeg_destroy_session_unlocked(my_obj,
        &my_obj->clnt_mgr[clnt_idx].session[i]);
  }

#ifdef LOAD_ADSP_RPC_LIB
  if (NULL != my_obj->adsprpc_lib_handle) {
    dlclose(my_obj->adsprpc_lib_handle);
    my_obj->adsprpc_lib_handle = NULL;
  }
#endif

  pthread_mutex_unlock(&my_obj->job_lock);

  /* invalidate client session */
  pthread_mutex_destroy(&my_obj->clnt_mgr[clnt_idx].lock);
  memset(&my_obj->clnt_mgr[clnt_idx], 0, sizeof(mm_jpeg_client_t));

  rc = 0;
  LOGD("X");
  return rc;
}

OMX_ERRORTYPE mm_jpeg_ebd(OMX_HANDLETYPE hComponent,
  OMX_PTR pAppData,
  OMX_BUFFERHEADERTYPE *pBuffer)
{
  mm_jpeg_job_session_t *p_session = (mm_jpeg_job_session_t *) pAppData;

  LOGH("count %d ", p_session->ebd_count);
  pthread_mutex_lock(&p_session->lock);
  p_session->ebd_count++;
  pthread_mutex_unlock(&p_session->lock);
  return 0;
}

OMX_ERRORTYPE mm_jpeg_fbd(OMX_HANDLETYPE hComponent,
  OMX_PTR pAppData,
  OMX_BUFFERHEADERTYPE *pBuffer)
{
  OMX_ERRORTYPE ret = OMX_ErrorNone;
  mm_jpeg_job_session_t *p_session = (mm_jpeg_job_session_t *) pAppData;
  mm_jpeg_output_t output_buf;
  LOGI("count %d ", p_session->fbd_count);
  LOGI("KPI Perf] : PROFILE_JPEG_FBD");

  pthread_mutex_lock(&p_session->lock);
  KPI_ATRACE_INT("Camera:JPEG",
      (int32_t)((uint32_t)GET_SESSION_IDX(
        p_session->sessionId)<<16 | --p_session->job_index));
  if (MM_JPEG_ABORT_NONE != p_session->abort_state) {
    pthread_mutex_unlock(&p_session->lock);
    return ret;
  }
#ifdef MM_JPEG_DUMP_OUT_BS
  char filename[256];
  static int bsc;
  snprintf(filename, sizeof(filename),
      QCAMERA_DUMP_FRM_LOCATION"jpeg/mm_jpeg_bs%d.jpg", bsc++);
  DUMP_TO_FILE(filename,
    pBuffer->pBuffer,
    (size_t)(uint32_t)pBuffer->nFilledLen);
#endif

  p_session->fbd_count++;
  if (NULL != p_session->params.jpeg_cb) {

    p_session->job_status = JPEG_JOB_STATUS_DONE;
    output_buf.buf_filled_len = (uint32_t)pBuffer->nFilledLen;
    output_buf.buf_vaddr = pBuffer->pBuffer;
    output_buf.fd = -1;
    LOGH("send jpeg callback %d buf 0x%p len %u JobID %u",
      p_session->job_status, pBuffer->pBuffer,
      (unsigned int)pBuffer->nFilledLen, p_session->jobId);
    p_session->params.jpeg_cb(p_session->job_status,
      p_session->client_hdl,
      p_session->jobId,
      &output_buf,
      p_session->params.userdata);

    mm_jpegenc_job_done(p_session);

    mm_jpeg_put_mem((void *)p_session);
  }
  pthread_mutex_unlock(&p_session->lock);

  return ret;
}



OMX_ERRORTYPE mm_jpeg_event_handler(OMX_HANDLETYPE hComponent,
  OMX_PTR pAppData,
  OMX_EVENTTYPE eEvent,
  OMX_U32 nData1,
  OMX_U32 nData2,
  OMX_PTR pEventData)
{
  mm_jpeg_job_session_t *p_session = (mm_jpeg_job_session_t *) pAppData;

  LOGD("%d %d %d state %d", eEvent, (int)nData1,
    (int)nData2, p_session->abort_state);

  pthread_mutex_lock(&p_session->lock);

  if (MM_JPEG_ABORT_INIT == p_session->abort_state) {
    p_session->abort_state = MM_JPEG_ABORT_DONE;
    pthread_cond_signal(&p_session->cond);
    pthread_mutex_unlock(&p_session->lock);
    return OMX_ErrorNone;
  }

  if (eEvent == OMX_EventError) {
    p_session->error_flag = nData2;
    if (p_session->encoding == OMX_TRUE) {
      LOGE("Error during encoding");

      /* send jpeg callback */
      if (NULL != p_session->params.jpeg_cb) {
        p_session->job_status = JPEG_JOB_STATUS_ERROR;
        LOGE("send jpeg error callback %d",
          p_session->job_status);
        p_session->params.jpeg_cb(p_session->job_status,
          p_session->client_hdl,
          p_session->jobId,
          NULL,
          p_session->params.userdata);
      }

      /* remove from ready queue */
      mm_jpegenc_job_done(p_session);
    }
    pthread_cond_signal(&p_session->cond);
  } else if (eEvent == OMX_EventCmdComplete) {
    if (p_session->state_change_pending == OMX_TRUE) {
      p_session->state_change_pending = OMX_FALSE;
      pthread_cond_signal(&p_session->cond);
    }
  }

  pthread_mutex_unlock(&p_session->lock);
  return OMX_ErrorNone;
}



/* remove the first job from the queue with matching client handle */
mm_jpeg_job_q_node_t* mm_jpeg_queue_remove_job_by_client_id(
  mm_jpeg_queue_t* queue, uint32_t client_hdl)
{
  mm_jpeg_q_node_t* node = NULL;
  mm_jpeg_job_q_node_t* data = NULL;
  mm_jpeg_job_q_node_t* job_node = NULL;
  struct cam_list *head = NULL;
  struct cam_list *pos = NULL;

  pthread_mutex_lock(&queue->lock);
  head = &queue->head.list;
  pos = head->next;
  while(pos != head) {
    node = member_of(pos, mm_jpeg_q_node_t, list);
    data = (mm_jpeg_job_q_node_t *)node->data.p;

    if (data && (data->enc_info.client_handle == client_hdl)) {
      LOGH("found matching client handle");
      job_node = data;
      cam_list_del_node(&node->list);
      queue->size--;
      free(node);
      LOGH("queue size = %d", queue->size);
      break;
    }
    pos = pos->next;
  }

  pthread_mutex_unlock(&queue->lock);

  return job_node;
}

/* remove the first job from the queue with matching session id */
mm_jpeg_job_q_node_t* mm_jpeg_queue_remove_job_by_session_id(
  mm_jpeg_queue_t* queue, uint32_t session_id)
{
  mm_jpeg_q_node_t* node = NULL;
  mm_jpeg_job_q_node_t* data = NULL;
  mm_jpeg_job_q_node_t* job_node = NULL;
  struct cam_list *head = NULL;
  struct cam_list *pos = NULL;

  pthread_mutex_lock(&queue->lock);
  head = &queue->head.list;
  pos = head->next;
  while(pos != head) {
    node = member_of(pos, mm_jpeg_q_node_t, list);
    data = (mm_jpeg_job_q_node_t *)node->data.p;

    if (data && (data->enc_info.encode_job.session_id == session_id)) {
      LOGH("found matching session id");
      job_node = data;
      cam_list_del_node(&node->list);
      queue->size--;
      free(node);
      LOGH("queue size = %d", queue->size);
      break;
    }
    pos = pos->next;
  }

  pthread_mutex_unlock(&queue->lock);

  return job_node;
}

/* remove job from the queue with matching job id */
mm_jpeg_job_q_node_t* mm_jpeg_queue_remove_job_by_job_id(
  mm_jpeg_queue_t* queue, uint32_t job_id)
{
  mm_jpeg_q_node_t* node = NULL;
  mm_jpeg_job_q_node_t* data = NULL;
  mm_jpeg_job_q_node_t* job_node = NULL;
  struct cam_list *head = NULL;
  struct cam_list *pos = NULL;
  uint32_t lq_job_id;

  pthread_mutex_lock(&queue->lock);
  head = &queue->head.list;
  pos = head->next;
  while(pos != head) {
    node = member_of(pos, mm_jpeg_q_node_t, list);
    data = (mm_jpeg_job_q_node_t *)node->data.p;

    if(NULL == data) {
      LOGE("Data is NULL");
      pthread_mutex_unlock(&queue->lock);
      return NULL;
    }

    if (data->type == MM_JPEG_CMD_TYPE_DECODE_JOB) {
      lq_job_id = data->dec_info.job_id;
    } else {
      lq_job_id = data->enc_info.job_id;
    }

    if (data && (lq_job_id == job_id)) {
      LOGD("found matching job id");
      job_node = data;
      cam_list_del_node(&node->list);
      queue->size--;
      free(node);
      break;
    }
    pos = pos->next;
  }

  pthread_mutex_unlock(&queue->lock);

  return job_node;
}

/* remove job from the queue with matching job id */
mm_jpeg_job_q_node_t* mm_jpeg_queue_remove_job_unlk(
  mm_jpeg_queue_t* queue, uint32_t job_id)
{
  mm_jpeg_q_node_t* node = NULL;
  mm_jpeg_job_q_node_t* data = NULL;
  mm_jpeg_job_q_node_t* job_node = NULL;
  struct cam_list *head = NULL;
  struct cam_list *pos = NULL;

  head = &queue->head.list;
  pos = head->next;
  while(pos != head) {
    node = member_of(pos, mm_jpeg_q_node_t, list);
    data = (mm_jpeg_job_q_node_t *)node->data.p;

    if (data && (data->enc_info.job_id == job_id)) {
      job_node = data;
      cam_list_del_node(&node->list);
      queue->size--;
      free(node);
      break;
    }
    pos = pos->next;
  }

  return job_node;
}
