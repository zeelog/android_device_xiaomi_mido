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

// To remove
#include <cutils/properties.h>

// System dependencies
#include <stdlib.h>
#include <pthread.h>

// JPEG dependencies
#include "mm_jpeg_dbg.h"
#include "mm_jpeg_interface.h"
#include "mm_jpeg.h"
#include "mm_jpeg_mpo.h"

static pthread_mutex_t g_intf_lock = PTHREAD_MUTEX_INITIALIZER;
static mm_jpeg_obj* g_jpeg_obj = NULL;

static pthread_mutex_t g_handler_lock = PTHREAD_MUTEX_INITIALIZER;
static uint16_t g_handler_history_count = 0; /* history count for handler */
volatile uint32_t gKpiDebugLevel = 0;

/** mm_jpeg_util_generate_handler:
 *
 *  Arguments:
 *    @index: client index
 *
 *  Return:
 *       handle value
 *
 *  Description:
 *       utility function to generate handler
 *
 **/
uint32_t mm_jpeg_util_generate_handler(uint8_t index)
{
  uint32_t handler = 0;
  pthread_mutex_lock(&g_handler_lock);
  g_handler_history_count++;
  if (0 == g_handler_history_count) {
    g_handler_history_count++;
  }
  handler = g_handler_history_count;
  handler = (handler<<8) | index;
  pthread_mutex_unlock(&g_handler_lock);
  return handler;
}

/** mm_jpeg_util_get_index_by_handler:
 *
 *  Arguments:
 *    @handler: handle value
 *
 *  Return:
 *       client index
 *
 *  Description:
 *       get client index
 *
 **/
uint8_t mm_jpeg_util_get_index_by_handler(uint32_t handler)
{
  return (handler & 0x000000ff);
}

/** mm_jpeg_intf_start_job:
 *
 *  Arguments:
 *    @client_hdl: client handle
 *    @job: jpeg job object
 *    @jobId: job id
 *
 *  Return:
 *       0 success, failure otherwise
 *
 *  Description:
 *       start the jpeg job
 *
 **/
static int32_t mm_jpeg_intf_start_job(mm_jpeg_job_t* job, uint32_t* job_id)
{
  int32_t rc = -1;

  if (NULL == job ||
    NULL == job_id) {
    LOGE("invalid parameters for job or jobId");
    return rc;
  }

  pthread_mutex_lock(&g_intf_lock);
  if (NULL == g_jpeg_obj) {
    /* mm_jpeg obj not exists, return error */
    LOGE("mm_jpeg is not opened yet");
    pthread_mutex_unlock(&g_intf_lock);
    return rc;
  }
  rc = mm_jpeg_start_job(g_jpeg_obj, job, job_id);
  pthread_mutex_unlock(&g_intf_lock);
  return rc;
}

/** mm_jpeg_intf_create_session:
 *
 *  Arguments:
 *    @client_hdl: client handle
 *    @p_params: encode parameters
 *    @p_session_id: session id
 *
 *  Return:
 *       0 success, failure otherwise
 *
 *  Description:
 *       Create new jpeg session
 *
 **/
static int32_t mm_jpeg_intf_create_session(uint32_t client_hdl,
    mm_jpeg_encode_params_t *p_params,
    uint32_t *p_session_id)
{
  int32_t rc = -1;

  if (0 == client_hdl || NULL == p_params || NULL == p_session_id) {
    LOGE("invalid client_hdl or jobId");
    return rc;
  }

  pthread_mutex_lock(&g_intf_lock);
  if (NULL == g_jpeg_obj) {
    /* mm_jpeg obj not exists, return error */
    LOGE("mm_jpeg is not opened yet");
    pthread_mutex_unlock(&g_intf_lock);
    return rc;
  }

 rc = mm_jpeg_create_session(g_jpeg_obj, client_hdl, p_params, p_session_id);
  pthread_mutex_unlock(&g_intf_lock);
  return rc;
}

/** mm_jpeg_intf_destroy_session:
 *
 *  Arguments:
 *    @session_id: session id
 *
 *  Return:
 *       0 success, failure otherwise
 *
 *  Description:
 *       Destroy jpeg session
 *
 **/
static int32_t mm_jpeg_intf_destroy_session(uint32_t session_id)
{
  int32_t rc = -1;

  if (0 == session_id) {
    LOGE("invalid client_hdl or jobId");
    return rc;
  }

  pthread_mutex_lock(&g_intf_lock);
  if (NULL == g_jpeg_obj) {
    /* mm_jpeg obj not exists, return error */
    LOGE("mm_jpeg is not opened yet");
    pthread_mutex_unlock(&g_intf_lock);
    return rc;
  }

  rc = mm_jpeg_destroy_session_by_id(g_jpeg_obj, session_id);
  pthread_mutex_unlock(&g_intf_lock);
  return rc;
}

/** mm_jpeg_intf_abort_job:
 *
 *  Arguments:
 *    @jobId: job id
 *
 *  Return:
 *       0 success, failure otherwise
 *
 *  Description:
 *       Abort the jpeg job
 *
 **/
static int32_t mm_jpeg_intf_abort_job(uint32_t job_id)
{
  int32_t rc = -1;

  if (0 == job_id) {
    LOGE("invalid jobId");
    return rc;
  }

  pthread_mutex_lock(&g_intf_lock);
  if (NULL == g_jpeg_obj) {
    /* mm_jpeg obj not exists, return error */
    LOGE("mm_jpeg is not opened yet");
    pthread_mutex_unlock(&g_intf_lock);
    return rc;
  }

  rc = mm_jpeg_abort_job(g_jpeg_obj, job_id);
  pthread_mutex_unlock(&g_intf_lock);
  return rc;
}

/** mm_jpeg_intf_close:
 *
 *  Arguments:
 *    @client_hdl: client handle
 *
 *  Return:
 *       0 success, failure otherwise
 *
 *  Description:
 *       Close the jpeg job
 *
 **/
static int32_t mm_jpeg_intf_close(uint32_t client_hdl)
{
  int32_t rc = -1;

  if (0 == client_hdl) {
    LOGE("invalid client_hdl");
    return rc;
  }

  pthread_mutex_lock(&g_intf_lock);
  if (NULL == g_jpeg_obj) {
    /* mm_jpeg obj not exists, return error */
    LOGE("mm_jpeg is not opened yet");
    pthread_mutex_unlock(&g_intf_lock);
    return rc;
  }

  rc = mm_jpeg_close(g_jpeg_obj, client_hdl);
  g_jpeg_obj->num_clients--;
  if(0 == rc) {
    if (0 == g_jpeg_obj->num_clients) {
      /* No client, close jpeg internally */
      rc = mm_jpeg_deinit(g_jpeg_obj);
      free(g_jpeg_obj);
      g_jpeg_obj = NULL;
    }
  }

  pthread_mutex_unlock(&g_intf_lock);
  return rc;
}

/** mm_jpeg_intf_compose_mpo:
 *
 *  Arguments:
 *    @mpo_info : MPO Information
 *
 *  Return:
 *       0 success, failure otherwise
 *
 *  Description:
 *       Compose MPO image from jpeg images
 *
 **/
static int32_t mm_jpeg_intf_compose_mpo(mm_jpeg_mpo_info_t *mpo_info)
{
  int32_t rc = -1;
  if (!mpo_info) {
    LOGE("Invalid input");
    return rc;
  }

  if (mpo_info->num_of_images > MM_JPEG_MAX_MPO_IMAGES) {
    LOGE("Num of images exceeds max supported images in MPO");
    return rc;
  }
  //Call MPo composition
  rc = mm_jpeg_mpo_compose(mpo_info);

  return rc;
}

/** jpeg_open:
 *
 *  Arguments:
 *    @ops: ops table pointer
 *    @mpo_ops: mpo ops table ptr
 *    @picture_size: Max available dim
 *    @jpeg_metadata: Jpeg meta data
 *
 *  Return:
 *       0 failure, success otherwise
 *
 *  Description:
 *       Open a jpeg client. Jpeg meta data will be cached
 *       but memory manegement has to be done by the cient.
 *
 **/
uint32_t jpeg_open(mm_jpeg_ops_t *ops, mm_jpeg_mpo_ops_t *mpo_ops,
  mm_dimension picture_size,
  cam_jpeg_metadata_t *jpeg_metadata)
{
  int32_t rc = 0;
  uint32_t clnt_hdl = 0;
  mm_jpeg_obj* jpeg_obj = NULL;
  char prop[PROPERTY_VALUE_MAX];

  property_get("persist.camera.kpi.debug", prop, "0");
  gKpiDebugLevel = atoi(prop);

  pthread_mutex_lock(&g_intf_lock);
  /* first time open */
  if(NULL == g_jpeg_obj) {
    jpeg_obj = (mm_jpeg_obj *)malloc(sizeof(mm_jpeg_obj));
    if(NULL == jpeg_obj) {
      LOGE("no mem");
      pthread_mutex_unlock(&g_intf_lock);
      return clnt_hdl;
    }

    /* initialize jpeg obj */
    memset(jpeg_obj, 0, sizeof(mm_jpeg_obj));

    /* by default reuse reproc source buffer if available */
    if (mpo_ops == NULL) {
      jpeg_obj->reuse_reproc_buffer = 1;
    } else {
      jpeg_obj->reuse_reproc_buffer = 0;
    }
   LOGH("reuse_reproc_buffer %d ",
      jpeg_obj->reuse_reproc_buffer);

    /* used for work buf calculation */
    jpeg_obj->max_pic_w = picture_size.w;
    jpeg_obj->max_pic_h = picture_size.h;

    /*Cache OTP Data for the session*/
    if (NULL != jpeg_metadata) {
      jpeg_obj->jpeg_metadata = jpeg_metadata;
    }

    rc = mm_jpeg_init(jpeg_obj);
    if(0 != rc) {
      LOGE("mm_jpeg_init err = %d", rc);
      free(jpeg_obj);
      pthread_mutex_unlock(&g_intf_lock);
      return clnt_hdl;
    }

    /* remember in global variable */
    g_jpeg_obj = jpeg_obj;
  }

  /* open new client */
  clnt_hdl = mm_jpeg_new_client(g_jpeg_obj);
  if (clnt_hdl > 0) {
    /* valid client */
    if (NULL != ops) {
      /* fill in ops tbl if ptr not NULL */
      ops->start_job = mm_jpeg_intf_start_job;
      ops->abort_job = mm_jpeg_intf_abort_job;
      ops->create_session = mm_jpeg_intf_create_session;
      ops->destroy_session = mm_jpeg_intf_destroy_session;
      ops->close = mm_jpeg_intf_close;
    }
    if (NULL != mpo_ops) {
      mpo_ops->compose_mpo = mm_jpeg_intf_compose_mpo;
    }
  } else {
    /* failed new client */
    LOGE("mm_jpeg_new_client failed");

    if (0 == g_jpeg_obj->num_clients) {
      /* no client, close jpeg */
      mm_jpeg_deinit(g_jpeg_obj);
      free(g_jpeg_obj);
      g_jpeg_obj = NULL;
    }
  }

  pthread_mutex_unlock(&g_intf_lock);
  return clnt_hdl;
}
