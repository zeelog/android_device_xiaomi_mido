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
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <dlfcn.h>
#define IOCTL_H <SYSTEM_HEADER_PREFIX/ioctl.h>
#include IOCTL_H

// Camera dependencies
#include "cam_semaphore.h"
#include "mm_camera_dbg.h"
#include "mm_camera_sock.h"
#include "mm_camera_interface.h"
#include "mm_camera.h"

#define SET_PARM_BIT32(parm, parm_arr) \
    (parm_arr[parm/32] |= (1<<(parm%32)))

#define GET_PARM_BIT32(parm, parm_arr) \
    ((parm_arr[parm/32]>>(parm%32))& 0x1)

/* internal function declare */
int32_t mm_camera_evt_sub(mm_camera_obj_t * my_obj,
                          uint8_t reg_flag);
int32_t mm_camera_enqueue_evt(mm_camera_obj_t *my_obj,
                              mm_camera_event_t *event);
extern mm_camera_obj_t* mm_camera_util_get_camera_by_session_id
        (uint32_t session_id);

/*===========================================================================
 * FUNCTION   : mm_camera_util_get_channel_by_handler
 *
 * DESCRIPTION: utility function to get a channel object from its handle
 *
 * PARAMETERS :
 *   @cam_obj: ptr to a camera object
 *   @handler: channel handle
 *
 * RETURN     : ptr to a channel object.
 *              NULL if failed.
 *==========================================================================*/
mm_channel_t * mm_camera_util_get_channel_by_handler(
                                    mm_camera_obj_t * cam_obj,
                                    uint32_t handler)
{
    int i;
    mm_channel_t *ch_obj = NULL;
    for(i = 0; i < MM_CAMERA_CHANNEL_MAX; i++) {
        if (handler == cam_obj->ch[i].my_hdl) {
            ch_obj = &cam_obj->ch[i];
            break;
        }
    }
    return ch_obj;
}

/*===========================================================================
 * FUNCTION   : mm_camera_util_chip_is_a_family
 *
 * DESCRIPTION: utility function to check if the host is A family chip
 *
 * PARAMETERS :
 *
 * RETURN     : TRUE if A family.
 *              FALSE otherwise.
 *==========================================================================*/
uint8_t mm_camera_util_chip_is_a_family(void)
{
#ifdef USE_A_FAMILY
    return TRUE;
#else
    return FALSE;
#endif
}

/*===========================================================================
 * FUNCTION   : mm_camera_dispatch_app_event
 *
 * DESCRIPTION: dispatch event to apps who regitster for event notify
 *
 * PARAMETERS :
 *   @cmd_cb: ptr to a struct storing event info
 *   @user_data: user data ptr (camera object)
 *
 * RETURN     : none
 *==========================================================================*/
static void mm_camera_dispatch_app_event(mm_camera_cmdcb_t *cmd_cb,
                                         void* user_data)
{
    int i;
    mm_camera_event_t *event = &cmd_cb->u.evt;
    mm_camera_obj_t * my_obj = (mm_camera_obj_t *)user_data;
    if (NULL != my_obj) {
        mm_camera_cmd_thread_name(my_obj->evt_thread.threadName);
        pthread_mutex_lock(&my_obj->cb_lock);
        for(i = 0; i < MM_CAMERA_EVT_ENTRY_MAX; i++) {
            if(my_obj->evt.evt[i].evt_cb) {
                my_obj->evt.evt[i].evt_cb(
                    my_obj->my_hdl,
                    event,
                    my_obj->evt.evt[i].user_data);
            }
        }
        pthread_mutex_unlock(&my_obj->cb_lock);
    }
}

/*===========================================================================
 * FUNCTION   : mm_camera_event_notify
 *
 * DESCRIPTION: callback to handle event notify from kernel. This call will
 *              dequeue event from kernel.
 *
 * PARAMETERS :
 *   @user_data: user data ptr (camera object)
 *
 * RETURN     : none
 *==========================================================================*/
static void mm_camera_event_notify(void* user_data)
{
    struct v4l2_event ev;
    struct msm_v4l2_event_data *msm_evt = NULL;
    int rc;
    mm_camera_event_t evt;
    memset(&evt, 0, sizeof(mm_camera_event_t));

    mm_camera_obj_t *my_obj = (mm_camera_obj_t*)user_data;
    if (NULL != my_obj) {
        /* read evt */
        memset(&ev, 0, sizeof(ev));
        rc = ioctl(my_obj->ctrl_fd, VIDIOC_DQEVENT, &ev);

        if (rc >= 0 && ev.id == MSM_CAMERA_MSM_NOTIFY) {
            msm_evt = (struct msm_v4l2_event_data *)ev.u.data;
            switch (msm_evt->command) {
            case CAM_EVENT_TYPE_DAEMON_PULL_REQ:
                evt.server_event_type = CAM_EVENT_TYPE_DAEMON_PULL_REQ;
                mm_camera_enqueue_evt(my_obj, &evt);
                break;
            case CAM_EVENT_TYPE_MAP_UNMAP_DONE:
                pthread_mutex_lock(&my_obj->evt_lock);
                my_obj->evt_rcvd.server_event_type = msm_evt->command;
                my_obj->evt_rcvd.status = msm_evt->status;
                pthread_cond_signal(&my_obj->evt_cond);
                pthread_mutex_unlock(&my_obj->evt_lock);
                break;
            case CAM_EVENT_TYPE_INT_TAKE_JPEG:
            case CAM_EVENT_TYPE_INT_TAKE_RAW:
                {
                    evt.server_event_type = msm_evt->command;
                    mm_camera_enqueue_evt(my_obj, &evt);
                }
                break;
            case MSM_CAMERA_PRIV_SHUTDOWN:
                {
                    LOGE("Camera Event DAEMON DIED received");
                    evt.server_event_type = CAM_EVENT_TYPE_DAEMON_DIED;
                    mm_camera_enqueue_evt(my_obj, &evt);
                }
                break;
            case CAM_EVENT_TYPE_CAC_DONE:
                {
                    evt.server_event_type = CAM_EVENT_TYPE_CAC_DONE;
                    mm_camera_enqueue_evt(my_obj, &evt);
                }
                break;
            default:
                break;
            }
        }
    }
}

/*===========================================================================
 * FUNCTION   : mm_camera_enqueue_evt
 *
 * DESCRIPTION: enqueue received event into event queue to be processed by
 *              event thread.
 *
 * PARAMETERS :
 *   @my_obj   : ptr to a camera object
 *   @event    : event to be queued
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_camera_enqueue_evt(mm_camera_obj_t *my_obj,
                              mm_camera_event_t *event)
{
    int32_t rc = 0;
    mm_camera_cmdcb_t *node = NULL;

    node = (mm_camera_cmdcb_t *)malloc(sizeof(mm_camera_cmdcb_t));
    if (NULL != node) {
        memset(node, 0, sizeof(mm_camera_cmdcb_t));
        node->cmd_type = MM_CAMERA_CMD_TYPE_EVT_CB;
        node->u.evt = *event;

        /* enqueue to evt cmd thread */
        cam_queue_enq(&(my_obj->evt_thread.cmd_queue), node);
        /* wake up evt cmd thread */
        cam_sem_post(&(my_obj->evt_thread.cmd_sem));
    } else {
        LOGE("No memory for mm_camera_node_t");
        rc = -1;
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_open
 *
 * DESCRIPTION: open a camera
 *
 * PARAMETERS :
 *   @my_obj   : ptr to a camera object
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_camera_open(mm_camera_obj_t *my_obj)
{
    char dev_name[MM_CAMERA_DEV_NAME_LEN];
    int32_t rc = 0;
    int8_t n_try=MM_CAMERA_DEV_OPEN_TRIES;
    uint8_t sleep_msec=MM_CAMERA_DEV_OPEN_RETRY_SLEEP;
    int cam_idx = 0;
    const char *dev_name_value = NULL;
    int l_errno = 0;

    LOGD("begin\n");

    if (NULL == my_obj) {
        goto on_error;
    }
    dev_name_value = mm_camera_util_get_dev_name(my_obj->my_hdl);
    if (NULL == dev_name_value) {
        goto on_error;
    }
    snprintf(dev_name, sizeof(dev_name), "/dev/%s",
             dev_name_value);
    sscanf(dev_name, "/dev/video%d", &cam_idx);
    LOGD("dev name = %s, cam_idx = %d", dev_name, cam_idx);

    do{
        n_try--;
        errno = 0;
        my_obj->ctrl_fd = open(dev_name, O_RDWR | O_NONBLOCK);
        l_errno = errno;
        LOGD("ctrl_fd = %d, errno == %d", my_obj->ctrl_fd, l_errno);
        if((my_obj->ctrl_fd >= 0) || (errno != EIO && errno != ETIMEDOUT) || (n_try <= 0 )) {
            break;
        }
        LOGE("Failed with %s error, retrying after %d milli-seconds",
              strerror(errno), sleep_msec);
        usleep(sleep_msec * 1000U);
    }while (n_try > 0);

    if (my_obj->ctrl_fd < 0) {
        LOGE("cannot open control fd of '%s' (%s)\n",
                  dev_name, strerror(l_errno));
        if (l_errno == EBUSY)
            rc = -EUSERS;
        else
            rc = -1;
        goto on_error;
    } else {
        mm_camera_get_session_id(my_obj, &my_obj->sessionid);
        LOGH("Camera Opened id = %d sessionid = %d", cam_idx, my_obj->sessionid);
    }

#ifdef DAEMON_PRESENT
    /* open domain socket*/
    n_try = MM_CAMERA_DEV_OPEN_TRIES;
    do {
        n_try--;
        my_obj->ds_fd = mm_camera_socket_create(cam_idx, MM_CAMERA_SOCK_TYPE_UDP);
        l_errno = errno;
        LOGD("ds_fd = %d, errno = %d", my_obj->ds_fd, l_errno);
        if((my_obj->ds_fd >= 0) || (n_try <= 0 )) {
            LOGD("opened, break out while loop");
            break;
        }
        LOGD("failed with I/O error retrying after %d milli-seconds",
              sleep_msec);
        usleep(sleep_msec * 1000U);
    } while (n_try > 0);

    if (my_obj->ds_fd < 0) {
        LOGE("cannot open domain socket fd of '%s'(%s)\n",
                  dev_name, strerror(l_errno));
        rc = -1;
        goto on_error;
    }
#else /* DAEMON_PRESENT */
    cam_status_t cam_status;
    cam_status = mm_camera_module_open_session(my_obj->sessionid,
            mm_camera_module_event_handler);
    if (cam_status < 0) {
        LOGE("Failed to open session");
        if (cam_status == CAM_STATUS_BUSY) {
            rc = -EUSERS;
        } else {
            rc = -1;
        }
        goto on_error;
    }
#endif /* DAEMON_PRESENT */

    pthread_mutex_init(&my_obj->msg_lock, NULL);
    pthread_mutex_init(&my_obj->cb_lock, NULL);
    pthread_mutex_init(&my_obj->evt_lock, NULL);
    pthread_cond_init(&my_obj->evt_cond, NULL);

    LOGD("Launch evt Thread in Cam Open");
    snprintf(my_obj->evt_thread.threadName, THREAD_NAME_SIZE, "CAM_Dispatch");
    mm_camera_cmd_thread_launch(&my_obj->evt_thread,
                                mm_camera_dispatch_app_event,
                                (void *)my_obj);

    /* launch event poll thread
     * we will add evt fd into event poll thread upon user first register for evt */
    LOGD("Launch evt Poll Thread in Cam Open");
    snprintf(my_obj->evt_poll_thread.threadName, THREAD_NAME_SIZE, "CAM_evntPoll");
    mm_camera_poll_thread_launch(&my_obj->evt_poll_thread,
                                 MM_CAMERA_POLL_TYPE_EVT);
    mm_camera_evt_sub(my_obj, TRUE);

    /* unlock cam_lock, we need release global intf_lock in camera_open(),
     * in order not block operation of other Camera in dual camera use case.*/
    pthread_mutex_unlock(&my_obj->cam_lock);
    LOGD("end (rc = %d)\n", rc);
    return rc;

on_error:

    if (NULL == dev_name_value) {
        LOGE("Invalid device name\n");
        rc = -1;
    }

    if (NULL == my_obj) {
        LOGE("Invalid camera object\n");
        rc = -1;
    } else {
        if (my_obj->ctrl_fd >= 0) {
            close(my_obj->ctrl_fd);
            my_obj->ctrl_fd = -1;
        }
#ifdef DAEMON_PRESENT
        if (my_obj->ds_fd >= 0) {
            mm_camera_socket_close(my_obj->ds_fd);
            my_obj->ds_fd = -1;
        }
#endif
    }

    /* unlock cam_lock, we need release global intf_lock in camera_open(),
     * in order not block operation of other Camera in dual camera use case.*/
    pthread_mutex_unlock(&my_obj->cam_lock);
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_close
 *
 * DESCRIPTION: enqueue received event into event queue to be processed by
 *              event thread.
 *
 * PARAMETERS :
 *   @my_obj   : ptr to a camera object
 *   @event    : event to be queued
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_camera_close(mm_camera_obj_t *my_obj)
{
    LOGD("unsubscribe evt");

#ifndef DAEMON_PRESENT
    mm_camera_module_close_session(my_obj->sessionid);
#endif /* DAEMON_PRESENT */

    mm_camera_evt_sub(my_obj, FALSE);

    LOGD("Close evt Poll Thread in Cam Close");
    mm_camera_poll_thread_release(&my_obj->evt_poll_thread);

    LOGD("Close evt cmd Thread in Cam Close");
    mm_camera_cmd_thread_release(&my_obj->evt_thread);

    if(my_obj->ctrl_fd >= 0) {
        close(my_obj->ctrl_fd);
        my_obj->ctrl_fd = -1;
    }

#ifdef DAEMON_PRESENT
    if(my_obj->ds_fd >= 0) {
        mm_camera_socket_close(my_obj->ds_fd);
        my_obj->ds_fd = -1;
    }
#endif

    pthread_mutex_destroy(&my_obj->msg_lock);
    pthread_mutex_destroy(&my_obj->cb_lock);
    pthread_mutex_destroy(&my_obj->evt_lock);
    pthread_cond_destroy(&my_obj->evt_cond);
    pthread_mutex_unlock(&my_obj->cam_lock);
    return 0;
}

/*===========================================================================
 * FUNCTION   : mm_camera_register_event_notify_internal
 *
 * DESCRIPTION: internal implementation for registering callback for event notify.
 *
 * PARAMETERS :
 *   @my_obj   : ptr to a camera object
 *   @evt_cb   : callback to be registered to handle event notify
 *   @user_data: user data ptr
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_camera_register_event_notify_internal(mm_camera_obj_t *my_obj,
                                                 mm_camera_event_notify_t evt_cb,
                                                 void * user_data)
{
    int i;
    int rc = -1;
    mm_camera_evt_obj_t *evt_array = NULL;

    pthread_mutex_lock(&my_obj->cb_lock);
    evt_array = &my_obj->evt;
    if(evt_cb) {
        /* this is reg case */
        for(i = 0; i < MM_CAMERA_EVT_ENTRY_MAX; i++) {
            if(evt_array->evt[i].user_data == NULL) {
                evt_array->evt[i].evt_cb = evt_cb;
                evt_array->evt[i].user_data = user_data;
                evt_array->reg_count++;
                rc = 0;
                break;
            }
        }
    } else {
        /* this is unreg case */
        for(i = 0; i < MM_CAMERA_EVT_ENTRY_MAX; i++) {
            if(evt_array->evt[i].user_data == user_data) {
                evt_array->evt[i].evt_cb = NULL;
                evt_array->evt[i].user_data = NULL;
                evt_array->reg_count--;
                rc = 0;
                break;
            }
        }
    }

    pthread_mutex_unlock(&my_obj->cb_lock);
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_register_event_notify
 *
 * DESCRIPTION: registering a callback for event notify.
 *
 * PARAMETERS :
 *   @my_obj   : ptr to a camera object
 *   @evt_cb   : callback to be registered to handle event notify
 *   @user_data: user data ptr
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_camera_register_event_notify(mm_camera_obj_t *my_obj,
                                        mm_camera_event_notify_t evt_cb,
                                        void * user_data)
{
    int rc = -1;
    rc = mm_camera_register_event_notify_internal(my_obj,
                                                  evt_cb,
                                                  user_data);
    pthread_mutex_unlock(&my_obj->cam_lock);
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_qbuf
 *
 * DESCRIPTION: enqueue buffer back to kernel
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *   @ch_id        : channel handle
 *   @buf          : buf ptr to be enqueued
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_camera_qbuf(mm_camera_obj_t *my_obj,
                       uint32_t ch_id,
                       mm_camera_buf_def_t *buf)
{
    int rc = -1;
    mm_channel_t * ch_obj = NULL;
    ch_obj = mm_camera_util_get_channel_by_handler(my_obj, ch_id);

    pthread_mutex_unlock(&my_obj->cam_lock);

    /* we always assume qbuf will be done before channel/stream is fully stopped
     * because qbuf is done within dataCB context
     * in order to avoid deadlock, we are not locking ch_lock for qbuf */
    if (NULL != ch_obj) {
        rc = mm_channel_qbuf(ch_obj, buf);
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_get_queued_buf_count
 *
 * DESCRIPTION: return queued buffer count
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *   @ch_id        : channel handle
 *   @stream_id : stream id
 *
 * RETURN     : queued buffer count
 *==========================================================================*/
int32_t mm_camera_get_queued_buf_count(mm_camera_obj_t *my_obj,
        uint32_t ch_id, uint32_t stream_id)
{
    int rc = -1;
    mm_channel_t * ch_obj = NULL;
    uint32_t payload;
    ch_obj = mm_camera_util_get_channel_by_handler(my_obj, ch_id);
    payload = stream_id;

    if (NULL != ch_obj) {
        pthread_mutex_lock(&ch_obj->ch_lock);
        pthread_mutex_unlock(&my_obj->cam_lock);
        rc = mm_channel_fsm_fn(ch_obj,
                MM_CHANNEL_EVT_GET_STREAM_QUEUED_BUF_COUNT,
                (void *)&payload,
                NULL);
    } else {
        pthread_mutex_unlock(&my_obj->cam_lock);
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_query_capability
 *
 * DESCRIPTION: query camera capability
 *
 * PARAMETERS :
 *   @my_obj: camera object
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_camera_query_capability(mm_camera_obj_t *my_obj)
{
    int32_t rc = 0;

#ifdef DAEMON_PRESENT
    struct v4l2_capability cap;
    /* get camera capabilities */
    memset(&cap, 0, sizeof(cap));
    rc = ioctl(my_obj->ctrl_fd, VIDIOC_QUERYCAP, &cap);
#else /* DAEMON_PRESENT */
    cam_shim_packet_t *shim_cmd;
    cam_shim_cmd_data shim_cmd_data;
    memset(&shim_cmd_data, 0, sizeof(shim_cmd_data));
    shim_cmd_data.command = MSM_CAMERA_PRIV_QUERY_CAP;
    shim_cmd_data.stream_id = 0;
    shim_cmd_data.value = NULL;
    shim_cmd = mm_camera_create_shim_cmd_packet(CAM_SHIM_GET_PARM,
            my_obj->sessionid,&shim_cmd_data);
    rc = mm_camera_module_send_cmd(shim_cmd);
    mm_camera_destroy_shim_cmd_packet(shim_cmd);
#endif /* DAEMON_PRESENT */
    if (rc != 0) {
        LOGE("cannot get camera capabilities, rc = %d, errno %d",
                rc, errno);
    }
    pthread_mutex_unlock(&my_obj->cam_lock);
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_set_parms
 *
 * DESCRIPTION: set parameters per camera
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *   @parms        : ptr to a param struct to be set to server
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 * NOTE       : Assume the parms struct buf is already mapped to server via
 *              domain socket. Corresponding fields of parameters to be set
 *              are already filled in by upper layer caller.
 *==========================================================================*/
int32_t mm_camera_set_parms(mm_camera_obj_t *my_obj,
                            parm_buffer_t *parms)
{
    int32_t rc = -1;
    int32_t value = 0;
    if (parms !=  NULL) {
        rc = mm_camera_util_s_ctrl(my_obj, 0, my_obj->ctrl_fd,
            CAM_PRIV_PARM, &value);
    }
    pthread_mutex_unlock(&my_obj->cam_lock);
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_get_parms
 *
 * DESCRIPTION: get parameters per camera
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *   @parms        : ptr to a param struct to be get from server
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 * NOTE       : Assume the parms struct buf is already mapped to server via
 *              domain socket. Parameters to be get from server are already
 *              filled in by upper layer caller. After this call, corresponding
 *              fields of requested parameters will be filled in by server with
 *              detailed information.
 *==========================================================================*/
int32_t mm_camera_get_parms(mm_camera_obj_t *my_obj,
                            parm_buffer_t *parms)
{
    int32_t rc = -1;
    int32_t value = 0;
    if (parms != NULL) {
        rc = mm_camera_util_g_ctrl(my_obj, 0, my_obj->ctrl_fd, CAM_PRIV_PARM, &value);
    }
    pthread_mutex_unlock(&my_obj->cam_lock);
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_do_auto_focus
 *
 * DESCRIPTION: performing auto focus
 *
 * PARAMETERS :
 *   @camera_handle: camera handle
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 * NOTE       : if this call success, we will always assume there will
 *              be an auto_focus event following up.
 *==========================================================================*/
int32_t mm_camera_do_auto_focus(mm_camera_obj_t *my_obj)
{
    int32_t rc = -1;
    int32_t value = 0;
    rc = mm_camera_util_s_ctrl(my_obj, 0, my_obj->ctrl_fd, CAM_PRIV_DO_AUTO_FOCUS, &value);
    pthread_mutex_unlock(&my_obj->cam_lock);
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_cancel_auto_focus
 *
 * DESCRIPTION: cancel auto focus
 *
 * PARAMETERS :
 *   @camera_handle: camera handle
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_camera_cancel_auto_focus(mm_camera_obj_t *my_obj)
{
    int32_t rc = -1;
    int32_t value = 0;
    rc = mm_camera_util_s_ctrl(my_obj, 0, my_obj->ctrl_fd, CAM_PRIV_CANCEL_AUTO_FOCUS, &value);
    pthread_mutex_unlock(&my_obj->cam_lock);
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_prepare_snapshot
 *
 * DESCRIPTION: prepare hardware for snapshot
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *   @do_af_flag   : flag indicating if AF is needed
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_camera_prepare_snapshot(mm_camera_obj_t *my_obj,
                                   int32_t do_af_flag)
{
    int32_t rc = -1;
    int32_t value = do_af_flag;
    rc = mm_camera_util_s_ctrl(my_obj, 0, my_obj->ctrl_fd, CAM_PRIV_PREPARE_SNAPSHOT, &value);
    pthread_mutex_unlock(&my_obj->cam_lock);
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_start_zsl_snapshot
 *
 * DESCRIPTION: start zsl snapshot
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_camera_start_zsl_snapshot(mm_camera_obj_t *my_obj)
{
    int32_t rc = -1;
    int32_t value = 0;

    rc = mm_camera_util_s_ctrl(my_obj, 0, my_obj->ctrl_fd,
             CAM_PRIV_START_ZSL_SNAPSHOT, &value);
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_stop_zsl_snapshot
 *
 * DESCRIPTION: stop zsl capture
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_camera_stop_zsl_snapshot(mm_camera_obj_t *my_obj)
{
    int32_t rc = -1;
    int32_t value;
    rc = mm_camera_util_s_ctrl(my_obj, 0, my_obj->ctrl_fd,
             CAM_PRIV_STOP_ZSL_SNAPSHOT, &value);
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_flush
 *
 * DESCRIPTION: flush the current camera state and buffers
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_camera_flush(mm_camera_obj_t *my_obj)
{
    int32_t rc = -1;
    int32_t value;
    rc = mm_camera_util_s_ctrl(my_obj, 0, my_obj->ctrl_fd,
            CAM_PRIV_FLUSH, &value);
    pthread_mutex_unlock(&my_obj->cam_lock);
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_add_channel
 *
 * DESCRIPTION: add a channel
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *   @attr         : bundle attribute of the channel if needed
 *   @channel_cb   : callback function for bundle data notify
 *   @userdata     : user data ptr
 *
 * RETURN     : uint32_t type of channel handle
 *              0  -- invalid channel handle, meaning the op failed
 *              >0 -- successfully added a channel with a valid handle
 * NOTE       : if no bundle data notify is needed, meaning each stream in the
 *              channel will have its own stream data notify callback, then
 *              attr, channel_cb, and userdata can be NULL. In this case,
 *              no matching logic will be performed in channel for the bundling.
 *==========================================================================*/
uint32_t mm_camera_add_channel(mm_camera_obj_t *my_obj,
                               mm_camera_channel_attr_t *attr,
                               mm_camera_buf_notify_t channel_cb,
                               void *userdata)
{
    mm_channel_t *ch_obj = NULL;
    uint8_t ch_idx = 0;
    uint32_t ch_hdl = 0;

    for(ch_idx = 0; ch_idx < MM_CAMERA_CHANNEL_MAX; ch_idx++) {
        if (MM_CHANNEL_STATE_NOTUSED == my_obj->ch[ch_idx].state) {
            ch_obj = &my_obj->ch[ch_idx];
            break;
        }
    }

    if (NULL != ch_obj) {
        /* initialize channel obj */
        memset(ch_obj, 0, sizeof(mm_channel_t));
        ch_hdl = mm_camera_util_generate_handler(ch_idx);
        ch_obj->my_hdl = ch_hdl;
        ch_obj->state = MM_CHANNEL_STATE_STOPPED;
        ch_obj->cam_obj = my_obj;
        pthread_mutex_init(&ch_obj->ch_lock, NULL);
        ch_obj->sessionid = my_obj->sessionid;
        mm_channel_init(ch_obj, attr, channel_cb, userdata);
    }

    pthread_mutex_unlock(&my_obj->cam_lock);

    return ch_hdl;
}

/*===========================================================================
 * FUNCTION   : mm_camera_del_channel
 *
 * DESCRIPTION: delete a channel by its handle
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *   @ch_id        : channel handle
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 * NOTE       : all streams in the channel should be stopped already before
 *              this channel can be deleted.
 *==========================================================================*/
int32_t mm_camera_del_channel(mm_camera_obj_t *my_obj,
                              uint32_t ch_id)
{
    int32_t rc = -1;
    mm_channel_t * ch_obj =
        mm_camera_util_get_channel_by_handler(my_obj, ch_id);

    if (NULL != ch_obj) {
        pthread_mutex_lock(&ch_obj->ch_lock);
        pthread_mutex_unlock(&my_obj->cam_lock);

        rc = mm_channel_fsm_fn(ch_obj,
                               MM_CHANNEL_EVT_DELETE,
                               NULL,
                               NULL);

        pthread_mutex_destroy(&ch_obj->ch_lock);
        memset(ch_obj, 0, sizeof(mm_channel_t));
    } else {
        pthread_mutex_unlock(&my_obj->cam_lock);
    }
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_get_bundle_info
 *
 * DESCRIPTION: query bundle info of the channel
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *   @ch_id        : channel handle
 *   @bundle_info  : bundle info to be filled in
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 * NOTE       : all streams in the channel should be stopped already before
 *              this channel can be deleted.
 *==========================================================================*/
int32_t mm_camera_get_bundle_info(mm_camera_obj_t *my_obj,
                                  uint32_t ch_id,
                                  cam_bundle_config_t *bundle_info)
{
    int32_t rc = -1;
    mm_channel_t * ch_obj =
        mm_camera_util_get_channel_by_handler(my_obj, ch_id);

    if (NULL != ch_obj) {
        pthread_mutex_lock(&ch_obj->ch_lock);
        pthread_mutex_unlock(&my_obj->cam_lock);

        rc = mm_channel_fsm_fn(ch_obj,
                               MM_CHANNEL_EVT_GET_BUNDLE_INFO,
                               (void *)bundle_info,
                               NULL);
    } else {
        pthread_mutex_unlock(&my_obj->cam_lock);
    }
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_link_stream
 *
 * DESCRIPTION: link a stream into a channel
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *   @ch_id        : channel handle
 *   @stream_id    : stream that will be linked
 *   @linked_ch_id : channel in which the stream will be linked
 *
 * RETURN     : uint32_t type of stream handle
 *              0  -- invalid stream handle, meaning the op failed
 *              >0 -- successfully linked a stream with a valid handle
 *==========================================================================*/
uint32_t mm_camera_link_stream(mm_camera_obj_t *my_obj,
        uint32_t ch_id,
        uint32_t stream_id,
        uint32_t linked_ch_id)
{
    uint32_t s_hdl = 0;
    mm_channel_t * ch_obj =
            mm_camera_util_get_channel_by_handler(my_obj, linked_ch_id);
    mm_channel_t * owner_obj =
            mm_camera_util_get_channel_by_handler(my_obj, ch_id);

    if ((NULL != ch_obj) && (NULL != owner_obj)) {
        pthread_mutex_lock(&ch_obj->ch_lock);
        pthread_mutex_unlock(&my_obj->cam_lock);

        mm_camera_stream_link_t stream_link;
        memset(&stream_link, 0, sizeof(mm_camera_stream_link_t));
        stream_link.ch = owner_obj;
        stream_link.stream_id = stream_id;
        mm_channel_fsm_fn(ch_obj,
                          MM_CHANNEL_EVT_LINK_STREAM,
                          (void*)&stream_link,
                          (void*)&s_hdl);
    } else {
        pthread_mutex_unlock(&my_obj->cam_lock);
    }

    return s_hdl;
}

/*===========================================================================
 * FUNCTION   : mm_camera_add_stream
 *
 * DESCRIPTION: add a stream into a channel
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *   @ch_id        : channel handle
 *
 * RETURN     : uint32_t type of stream handle
 *              0  -- invalid stream handle, meaning the op failed
 *              >0 -- successfully added a stream with a valid handle
 *==========================================================================*/
uint32_t mm_camera_add_stream(mm_camera_obj_t *my_obj,
                              uint32_t ch_id)
{
    uint32_t s_hdl = 0;
    mm_channel_t * ch_obj =
        mm_camera_util_get_channel_by_handler(my_obj, ch_id);

    if (NULL != ch_obj) {
        pthread_mutex_lock(&ch_obj->ch_lock);
        pthread_mutex_unlock(&my_obj->cam_lock);

        mm_channel_fsm_fn(ch_obj,
                          MM_CHANNEL_EVT_ADD_STREAM,
                          NULL,
                          (void *)&s_hdl);
    } else {
        pthread_mutex_unlock(&my_obj->cam_lock);
    }

    return s_hdl;
}

/*===========================================================================
 * FUNCTION   : mm_camera_del_stream
 *
 * DESCRIPTION: delete a stream by its handle
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *   @ch_id        : channel handle
 *   @stream_id    : stream handle
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 * NOTE       : stream should be stopped already before it can be deleted.
 *==========================================================================*/
int32_t mm_camera_del_stream(mm_camera_obj_t *my_obj,
                             uint32_t ch_id,
                             uint32_t stream_id)
{
    int32_t rc = -1;
    mm_channel_t * ch_obj =
        mm_camera_util_get_channel_by_handler(my_obj, ch_id);

    if (NULL != ch_obj) {
        pthread_mutex_lock(&ch_obj->ch_lock);
        pthread_mutex_unlock(&my_obj->cam_lock);

        rc = mm_channel_fsm_fn(ch_obj,
                               MM_CHANNEL_EVT_DEL_STREAM,
                               (void *)&stream_id,
                               NULL);
    } else {
        pthread_mutex_unlock(&my_obj->cam_lock);
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_start_zsl_snapshot_ch
 *
 * DESCRIPTION: starts zsl snapshot for specific channel
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *   @ch_id        : channel handle
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_camera_start_zsl_snapshot_ch(mm_camera_obj_t *my_obj,
        uint32_t ch_id)
{
    int32_t rc = -1;
    mm_channel_t * ch_obj =
        mm_camera_util_get_channel_by_handler(my_obj, ch_id);

    if (NULL != ch_obj) {
        pthread_mutex_lock(&ch_obj->ch_lock);
        pthread_mutex_unlock(&my_obj->cam_lock);

        rc = mm_channel_fsm_fn(ch_obj,
                               MM_CHANNEL_EVT_START_ZSL_SNAPSHOT,
                               NULL,
                               NULL);
    } else {
        pthread_mutex_unlock(&my_obj->cam_lock);
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_stop_zsl_snapshot_ch
 *
 * DESCRIPTION: stops zsl snapshot for specific channel
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *   @ch_id        : channel handle
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_camera_stop_zsl_snapshot_ch(mm_camera_obj_t *my_obj,
        uint32_t ch_id)
{
    int32_t rc = -1;
    mm_channel_t * ch_obj =
        mm_camera_util_get_channel_by_handler(my_obj, ch_id);

    if (NULL != ch_obj) {
        pthread_mutex_lock(&ch_obj->ch_lock);
        pthread_mutex_unlock(&my_obj->cam_lock);

        rc = mm_channel_fsm_fn(ch_obj,
                               MM_CHANNEL_EVT_STOP_ZSL_SNAPSHOT,
                               NULL,
                               NULL);
    } else {
        pthread_mutex_unlock(&my_obj->cam_lock);
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_config_stream
 *
 * DESCRIPTION: configure a stream
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *   @ch_id        : channel handle
 *   @stream_id    : stream handle
 *   @config       : stream configuration
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_camera_config_stream(mm_camera_obj_t *my_obj,
                                uint32_t ch_id,
                                uint32_t stream_id,
                                mm_camera_stream_config_t *config)
{
    int32_t rc = -1;
    mm_channel_t * ch_obj =
        mm_camera_util_get_channel_by_handler(my_obj, ch_id);
    mm_evt_paylod_config_stream_t payload;

    if (NULL != ch_obj) {
        pthread_mutex_lock(&ch_obj->ch_lock);
        pthread_mutex_unlock(&my_obj->cam_lock);

        memset(&payload, 0, sizeof(mm_evt_paylod_config_stream_t));
        payload.stream_id = stream_id;
        payload.config = config;
        rc = mm_channel_fsm_fn(ch_obj,
                               MM_CHANNEL_EVT_CONFIG_STREAM,
                               (void *)&payload,
                               NULL);
    } else {
        pthread_mutex_unlock(&my_obj->cam_lock);
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_start_channel
 *
 * DESCRIPTION: start a channel, which will start all streams in the channel
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *   @ch_id        : channel handle
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_camera_start_channel(mm_camera_obj_t *my_obj, uint32_t ch_id)
{
    int32_t rc = -1;
    mm_channel_t * ch_obj =
        mm_camera_util_get_channel_by_handler(my_obj, ch_id);

    if (NULL != ch_obj) {
        pthread_mutex_lock(&ch_obj->ch_lock);
        pthread_mutex_unlock(&my_obj->cam_lock);

        rc = mm_channel_fsm_fn(ch_obj,
                               MM_CHANNEL_EVT_START,
                               NULL,
                               NULL);
    } else {
        pthread_mutex_unlock(&my_obj->cam_lock);
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_stop_channel
 *
 * DESCRIPTION: stop a channel, which will stop all streams in the channel
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *   @ch_id        : channel handle
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_camera_stop_channel(mm_camera_obj_t *my_obj,
                               uint32_t ch_id)
{
    int32_t rc = 0;
    mm_channel_t * ch_obj =
        mm_camera_util_get_channel_by_handler(my_obj, ch_id);

    if (NULL != ch_obj) {
        pthread_mutex_lock(&ch_obj->ch_lock);
        pthread_mutex_unlock(&my_obj->cam_lock);

        rc = mm_channel_fsm_fn(ch_obj,
                               MM_CHANNEL_EVT_STOP,
                               NULL,
                               NULL);
    } else {
        pthread_mutex_unlock(&my_obj->cam_lock);
    }
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_request_super_buf
 *
 * DESCRIPTION: for burst mode in bundle, reuqest certain amount of matched
 *              frames from superbuf queue
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *   @ch_id        : channel handle
 *   @num_buf_requested : number of matched frames needed
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_camera_request_super_buf(mm_camera_obj_t *my_obj,
        uint32_t ch_id, mm_camera_req_buf_t *buf)
{
    int32_t rc = -1;
    mm_channel_t * ch_obj =
        mm_camera_util_get_channel_by_handler(my_obj, ch_id);

    if ((NULL != ch_obj) && (buf != NULL)) {
        pthread_mutex_lock(&ch_obj->ch_lock);
        pthread_mutex_unlock(&my_obj->cam_lock);

        rc = mm_channel_fsm_fn(ch_obj, MM_CHANNEL_EVT_REQUEST_SUPER_BUF,
                (void *)buf, NULL);
    } else {
        pthread_mutex_unlock(&my_obj->cam_lock);
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_cancel_super_buf_request
 *
 * DESCRIPTION: for burst mode in bundle, cancel the reuqest for certain amount
 *              of matched frames from superbuf queue
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *   @ch_id        : channel handle
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_camera_cancel_super_buf_request(mm_camera_obj_t *my_obj, uint32_t ch_id)
{
    int32_t rc = -1;
    mm_channel_t * ch_obj =
        mm_camera_util_get_channel_by_handler(my_obj, ch_id);

    if (NULL != ch_obj) {
        pthread_mutex_lock(&ch_obj->ch_lock);
        pthread_mutex_unlock(&my_obj->cam_lock);

        rc = mm_channel_fsm_fn(ch_obj,
                               MM_CHANNEL_EVT_CANCEL_REQUEST_SUPER_BUF,
                               NULL,
                               NULL);
    } else {
        pthread_mutex_unlock(&my_obj->cam_lock);
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_flush_super_buf_queue
 *
 * DESCRIPTION: flush out all frames in the superbuf queue
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *   @ch_id        : channel handle
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_camera_flush_super_buf_queue(mm_camera_obj_t *my_obj, uint32_t ch_id,
                                                             uint32_t frame_idx)
{
    int32_t rc = -1;
    mm_channel_t * ch_obj =
        mm_camera_util_get_channel_by_handler(my_obj, ch_id);

    if (NULL != ch_obj) {
        pthread_mutex_lock(&ch_obj->ch_lock);
        pthread_mutex_unlock(&my_obj->cam_lock);

        rc = mm_channel_fsm_fn(ch_obj,
                               MM_CHANNEL_EVT_FLUSH_SUPER_BUF_QUEUE,
                               (void *)&frame_idx,
                               NULL);
    } else {
        pthread_mutex_unlock(&my_obj->cam_lock);
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_config_channel_notify
 *
 * DESCRIPTION: configures the channel notification mode
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *   @ch_id        : channel handle
 *   @notify_mode  : notification mode
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_camera_config_channel_notify(mm_camera_obj_t *my_obj,
                                        uint32_t ch_id,
                                        mm_camera_super_buf_notify_mode_t notify_mode)
{
    int32_t rc = -1;
    mm_channel_t * ch_obj =
        mm_camera_util_get_channel_by_handler(my_obj, ch_id);

    if (NULL != ch_obj) {
        pthread_mutex_lock(&ch_obj->ch_lock);
        pthread_mutex_unlock(&my_obj->cam_lock);

        rc = mm_channel_fsm_fn(ch_obj,
                               MM_CHANNEL_EVT_CONFIG_NOTIFY_MODE,
                               (void *)&notify_mode,
                               NULL);
    } else {
        pthread_mutex_unlock(&my_obj->cam_lock);
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_set_stream_parms
 *
 * DESCRIPTION: set parameters per stream
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *   @ch_id        : channel handle
 *   @s_id         : stream handle
 *   @parms        : ptr to a param struct to be set to server
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 * NOTE       : Assume the parms struct buf is already mapped to server via
 *              domain socket. Corresponding fields of parameters to be set
 *              are already filled in by upper layer caller.
 *==========================================================================*/
int32_t mm_camera_set_stream_parms(mm_camera_obj_t *my_obj,
                                   uint32_t ch_id,
                                   uint32_t s_id,
                                   cam_stream_parm_buffer_t *parms)
{
    int32_t rc = -1;
    mm_evt_paylod_set_get_stream_parms_t payload;
    mm_channel_t * ch_obj =
        mm_camera_util_get_channel_by_handler(my_obj, ch_id);

    if (NULL != ch_obj) {
        pthread_mutex_lock(&ch_obj->ch_lock);
        pthread_mutex_unlock(&my_obj->cam_lock);

        memset(&payload, 0, sizeof(payload));
        payload.stream_id = s_id;
        payload.parms = parms;

        rc = mm_channel_fsm_fn(ch_obj,
                               MM_CHANNEL_EVT_SET_STREAM_PARM,
                               (void *)&payload,
                               NULL);
    } else {
        pthread_mutex_unlock(&my_obj->cam_lock);
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_get_stream_parms
 *
 * DESCRIPTION: get parameters per stream
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *   @ch_id        : channel handle
 *   @s_id         : stream handle
 *   @parms        : ptr to a param struct to be get from server
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 * NOTE       : Assume the parms struct buf is already mapped to server via
 *              domain socket. Parameters to be get from server are already
 *              filled in by upper layer caller. After this call, corresponding
 *              fields of requested parameters will be filled in by server with
 *              detailed information.
 *==========================================================================*/
int32_t mm_camera_get_stream_parms(mm_camera_obj_t *my_obj,
                                   uint32_t ch_id,
                                   uint32_t s_id,
                                   cam_stream_parm_buffer_t *parms)
{
    int32_t rc = -1;
    mm_evt_paylod_set_get_stream_parms_t payload;
    mm_channel_t * ch_obj =
        mm_camera_util_get_channel_by_handler(my_obj, ch_id);

    if (NULL != ch_obj) {
        pthread_mutex_lock(&ch_obj->ch_lock);
        pthread_mutex_unlock(&my_obj->cam_lock);

        memset(&payload, 0, sizeof(payload));
        payload.stream_id = s_id;
        payload.parms = parms;

        rc = mm_channel_fsm_fn(ch_obj,
                               MM_CHANNEL_EVT_GET_STREAM_PARM,
                               (void *)&payload,
                               NULL);
    } else {
        pthread_mutex_unlock(&my_obj->cam_lock);
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_do_stream_action
 *
 * DESCRIPTION: request server to perform stream based action. Maybe removed later
 *              if the functionality is included in mm_camera_set_parms
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *   @ch_id        : channel handle
 *   @s_id         : stream handle
 *   @actions      : ptr to an action struct buf to be performed by server
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 * NOTE       : Assume the action struct buf is already mapped to server via
 *              domain socket. Actions to be performed by server are already
 *              filled in by upper layer caller.
 *==========================================================================*/
int32_t mm_camera_do_stream_action(mm_camera_obj_t *my_obj,
                                   uint32_t ch_id,
                                   uint32_t stream_id,
                                   void *actions)
{
    int32_t rc = -1;
    mm_evt_paylod_do_stream_action_t payload;
    mm_channel_t * ch_obj =
        mm_camera_util_get_channel_by_handler(my_obj, ch_id);

    if (NULL != ch_obj) {
        pthread_mutex_lock(&ch_obj->ch_lock);
        pthread_mutex_unlock(&my_obj->cam_lock);

        memset(&payload, 0, sizeof(payload));
        payload.stream_id = stream_id;
        payload.actions = actions;

        rc = mm_channel_fsm_fn(ch_obj,
                               MM_CHANNEL_EVT_DO_STREAM_ACTION,
                               (void*)&payload,
                               NULL);
    } else {
        pthread_mutex_unlock(&my_obj->cam_lock);
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_map_stream_buf
 *
 * DESCRIPTION: mapping stream buffer via domain socket to server
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *   @ch_id        : channel handle
 *   @s_id         : stream handle
 *   @buf_type     : type of buffer to be mapped. could be following values:
 *                   CAM_MAPPING_BUF_TYPE_STREAM_BUF
 *                   CAM_MAPPING_BUF_TYPE_STREAM_INFO
 *                   CAM_MAPPING_BUF_TYPE_OFFLINE_INPUT_BUF
 *   @buf_idx      : index of buffer within the stream buffers, only valid if
 *                   buf_type is CAM_MAPPING_BUF_TYPE_STREAM_BUF or
 *                   CAM_MAPPING_BUF_TYPE_OFFLINE_INPUT_BUF
 *   @plane_idx    : plane index. If all planes share the same fd,
 *                   plane_idx = -1; otherwise, plean_idx is the
 *                   index to plane (0..num_of_planes)
 *   @fd           : file descriptor of the buffer
 *   @size         : size of the buffer
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_camera_map_stream_buf(mm_camera_obj_t *my_obj,
                                 uint32_t ch_id,
                                 uint32_t stream_id,
                                 uint8_t buf_type,
                                 uint32_t buf_idx,
                                 int32_t plane_idx,
                                 int fd,
                                 size_t size,
                                 void *buffer)
{
    int32_t rc = -1;
    cam_buf_map_type payload;
    mm_channel_t * ch_obj =
        mm_camera_util_get_channel_by_handler(my_obj, ch_id);

    if (NULL != ch_obj) {
        pthread_mutex_lock(&ch_obj->ch_lock);
        pthread_mutex_unlock(&my_obj->cam_lock);

        memset(&payload, 0, sizeof(payload));
        payload.stream_id = stream_id;
        payload.type = buf_type;
        payload.frame_idx = buf_idx;
        payload.plane_idx = plane_idx;
        payload.fd = fd;
        payload.size = size;
        payload.buffer = buffer;
        rc = mm_channel_fsm_fn(ch_obj,
                               MM_CHANNEL_EVT_MAP_STREAM_BUF,
                               (void*)&payload,
                               NULL);
    } else {
        pthread_mutex_unlock(&my_obj->cam_lock);
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_map_stream_bufs
 *
 * DESCRIPTION: mapping stream buffers via domain socket to server
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *   @ch_id        : channel handle
 *   @buf_map_list : list of buffers to be mapped
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_camera_map_stream_bufs(mm_camera_obj_t *my_obj,
                                  uint32_t ch_id,
                                  const cam_buf_map_type_list *buf_map_list)
{
    int32_t rc = -1;
    cam_buf_map_type_list payload;
    mm_channel_t * ch_obj =
        mm_camera_util_get_channel_by_handler(my_obj, ch_id);

    if (NULL != ch_obj) {
        pthread_mutex_lock(&ch_obj->ch_lock);
        pthread_mutex_unlock(&my_obj->cam_lock);

        memcpy(&payload, buf_map_list, sizeof(payload));
        rc = mm_channel_fsm_fn(ch_obj,
                               MM_CHANNEL_EVT_MAP_STREAM_BUFS,
                               (void*)&payload,
                               NULL);
    } else {
        pthread_mutex_unlock(&my_obj->cam_lock);
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_unmap_stream_buf
 *
 * DESCRIPTION: unmapping stream buffer via domain socket to server
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *   @ch_id        : channel handle
 *   @s_id         : stream handle
 *   @buf_type     : type of buffer to be mapped. could be following values:
 *                   CAM_MAPPING_BUF_TYPE_STREAM_BUF
 *                   CAM_MAPPING_BUF_TYPE_STREAM_INFO
 *                   CAM_MAPPING_BUF_TYPE_OFFLINE_INPUT_BUF
 *   @buf_idx      : index of buffer within the stream buffers, only valid if
 *                   buf_type is CAM_MAPPING_BUF_TYPE_STREAM_BUF or
 *                   CAM_MAPPING_BUF_TYPE_OFFLINE_INPUT_BUF
 *   @plane_idx    : plane index. If all planes share the same fd,
 *                   plane_idx = -1; otherwise, plean_idx is the
 *                   index to plane (0..num_of_planes)
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_camera_unmap_stream_buf(mm_camera_obj_t *my_obj,
                                   uint32_t ch_id,
                                   uint32_t stream_id,
                                   uint8_t buf_type,
                                   uint32_t buf_idx,
                                   int32_t plane_idx)
{
    int32_t rc = -1;
    cam_buf_unmap_type payload;
    mm_channel_t * ch_obj =
        mm_camera_util_get_channel_by_handler(my_obj, ch_id);

    if (NULL != ch_obj) {
        pthread_mutex_lock(&ch_obj->ch_lock);
        pthread_mutex_unlock(&my_obj->cam_lock);

        memset(&payload, 0, sizeof(payload));
        payload.stream_id = stream_id;
        payload.type = buf_type;
        payload.frame_idx = buf_idx;
        payload.plane_idx = plane_idx;
        rc = mm_channel_fsm_fn(ch_obj,
                               MM_CHANNEL_EVT_UNMAP_STREAM_BUF,
                               (void*)&payload,
                               NULL);
    } else {
        pthread_mutex_unlock(&my_obj->cam_lock);
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_evt_sub
 *
 * DESCRIPTION: subscribe/unsubscribe event notify from kernel
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *   @reg_flag     : 1 -- subscribe ; 0 -- unsubscribe
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_camera_evt_sub(mm_camera_obj_t * my_obj,
                          uint8_t reg_flag)
{
    int32_t rc = 0;
    struct v4l2_event_subscription sub;

    memset(&sub, 0, sizeof(sub));
    sub.type = MSM_CAMERA_V4L2_EVENT_TYPE;
    sub.id = MSM_CAMERA_MSM_NOTIFY;
    if(FALSE == reg_flag) {
        /* unsubscribe */
        rc = ioctl(my_obj->ctrl_fd, VIDIOC_UNSUBSCRIBE_EVENT, &sub);
        if (rc < 0) {
            LOGE("unsubscribe event rc = %d, errno %d",
                     rc, errno);
            return rc;
        }
        /* remove evt fd from the polling thraed when unreg the last event */
        rc = mm_camera_poll_thread_del_poll_fd(&my_obj->evt_poll_thread,
                                               my_obj->my_hdl,
                                               mm_camera_sync_call);
    } else {
        rc = ioctl(my_obj->ctrl_fd, VIDIOC_SUBSCRIBE_EVENT, &sub);
        if (rc < 0) {
            LOGE("subscribe event rc = %d, errno %d",
             rc, errno);
            return rc;
        }
        /* add evt fd to polling thread when subscribe the first event */
        rc = mm_camera_poll_thread_add_poll_fd(&my_obj->evt_poll_thread,
                                               my_obj->my_hdl,
                                               my_obj->ctrl_fd,
                                               mm_camera_event_notify,
                                               (void*)my_obj,
                                               mm_camera_sync_call);
    }
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_util_wait_for_event
 *
 * DESCRIPTION: utility function to wait for certain events
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *   @evt_mask     : mask for events to be waited. Any of event in the mask would
 *                   trigger the wait to end
 *   @status       : status of the event
 *
 * RETURN     : none
 *==========================================================================*/
void mm_camera_util_wait_for_event(mm_camera_obj_t *my_obj,
                                   uint32_t evt_mask,
                                   uint32_t *status)
{
    int32_t rc = 0;
    struct timespec ts;

    pthread_mutex_lock(&my_obj->evt_lock);
    while (!(my_obj->evt_rcvd.server_event_type & evt_mask)) {
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += WAIT_TIMEOUT;
        rc = pthread_cond_timedwait(&my_obj->evt_cond, &my_obj->evt_lock, &ts);
        if (rc) {
            LOGE("pthread_cond_timedwait of evt_mask 0x%x failed %d",
                     evt_mask, rc);
            break;
        }
    }
    if (!rc) {
        *status = my_obj->evt_rcvd.status;
    } else {
        *status = MSM_CAMERA_STATUS_FAIL;
    }
    /* reset local storage for recieved event for next event */
    memset(&my_obj->evt_rcvd, 0, sizeof(mm_camera_event_t));
    pthread_mutex_unlock(&my_obj->evt_lock);
}

/*===========================================================================
 * FUNCTION   : mm_camera_util_bundled_sendmsg
 *
 * DESCRIPTION: utility function to send bundled msg via domain socket
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *   @msg          : message to be sent
 *   @buf_size     : size of the message to be sent
 *   @sendfds      : array of file descriptors to be sent
 *   @numfds       : number of file descriptors to be sent
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_camera_util_bundled_sendmsg(mm_camera_obj_t *my_obj,
                                       void *msg,
                                       size_t buf_size,
                                       int sendfds[CAM_MAX_NUM_BUFS_PER_STREAM],
                                       int numfds)
{
    int32_t rc = -1;
    uint32_t status;

    /* need to lock msg_lock, since sendmsg until response back is deemed as one operation*/
    pthread_mutex_lock(&my_obj->msg_lock);
    if(mm_camera_socket_bundle_sendmsg(my_obj->ds_fd, msg, buf_size, sendfds, numfds) > 0) {
        /* wait for event that mapping/unmapping is done */
        mm_camera_util_wait_for_event(my_obj, CAM_EVENT_TYPE_MAP_UNMAP_DONE, &status);
        if (MSM_CAMERA_STATUS_SUCCESS == status) {
            rc = 0;
        }
    }
    pthread_mutex_unlock(&my_obj->msg_lock);
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_util_sendmsg
 *
 * DESCRIPTION: utility function to send msg via domain socket
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *   @msg          : message to be sent
 *   @buf_size     : size of the message to be sent
 *   @sendfd       : >0 if any file descriptor need to be passed across process
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_camera_util_sendmsg(mm_camera_obj_t *my_obj,
                               void *msg,
                               size_t buf_size,
                               int sendfd)
{
    int32_t rc = -1;
    uint32_t status;

    /* need to lock msg_lock, since sendmsg until reposonse back is deemed as one operation*/
    pthread_mutex_lock(&my_obj->msg_lock);
    if(mm_camera_socket_sendmsg(my_obj->ds_fd, msg, buf_size, sendfd) > 0) {
        /* wait for event that mapping/unmapping is done */
        mm_camera_util_wait_for_event(my_obj, CAM_EVENT_TYPE_MAP_UNMAP_DONE, &status);
        if (MSM_CAMERA_STATUS_SUCCESS == status) {
            rc = 0;
        }
    }
    pthread_mutex_unlock(&my_obj->msg_lock);
    return rc;
}

/*===========================================================================
 * FUNCTIOa   : mm_camera_map_buf
 *
 * DESCRIPTION: mapping camera buffer via domain socket to server
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *   @buf_type     : type of buffer to be mapped. could be following values:
 *                   CAM_MAPPING_BUF_TYPE_CAPABILITY
 *                   CAM_MAPPING_BUF_TYPE_SETPARM_BUF
 *                   CAM_MAPPING_BUF_TYPE_GETPARM_BUF
 *   @fd           : file descriptor of the buffer
 *   @size         : size of the buffer
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_camera_map_buf(mm_camera_obj_t *my_obj,
        uint8_t buf_type, int fd, size_t size, void *buffer)
{
    int32_t rc = 0;

    cam_sock_packet_t packet;
    memset(&packet, 0, sizeof(cam_sock_packet_t));
    packet.msg_type = CAM_MAPPING_TYPE_FD_MAPPING;
    packet.payload.buf_map.type = buf_type;
    packet.payload.buf_map.fd = fd;
    packet.payload.buf_map.size = size;
    packet.payload.buf_map.buffer = buffer;
#ifdef DAEMON_PRESENT
    rc = mm_camera_util_sendmsg(my_obj,
                                &packet,
                                sizeof(cam_sock_packet_t),
                                fd);
#else
    cam_shim_packet_t *shim_cmd;
    shim_cmd = mm_camera_create_shim_cmd_packet(CAM_SHIM_REG_BUF,
            my_obj->sessionid, &packet);
    rc = mm_camera_module_send_cmd(shim_cmd);
    mm_camera_destroy_shim_cmd_packet(shim_cmd);
#endif
    pthread_mutex_unlock(&my_obj->cam_lock);
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_map_bufs
 *
 * DESCRIPTION: mapping camera buffers via domain socket to server
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *   @buf_map_list : list of buffers to be mapped
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_camera_map_bufs(mm_camera_obj_t *my_obj,
                           const cam_buf_map_type_list* buf_map_list)
{
    int32_t rc = 0;
    cam_sock_packet_t packet;
    memset(&packet, 0, sizeof(cam_sock_packet_t));
    packet.msg_type = CAM_MAPPING_TYPE_FD_BUNDLED_MAPPING;

    memcpy(&packet.payload.buf_map_list, buf_map_list,
           sizeof(packet.payload.buf_map_list));

    int sendfds[CAM_MAX_NUM_BUFS_PER_STREAM];
    uint32_t numbufs = packet.payload.buf_map_list.length;
    uint32_t i;
    for (i = 0; i < numbufs; i++) {
        sendfds[i] = packet.payload.buf_map_list.buf_maps[i].fd;
        packet.payload.buf_map_list.buf_maps[i].buffer =
                buf_map_list->buf_maps[i].buffer;
    }
    for (i = numbufs; i < CAM_MAX_NUM_BUFS_PER_STREAM; i++) {
        packet.payload.buf_map_list.buf_maps[i].fd = -1;
        sendfds[i] = -1;
    }

#ifdef DAEMON_PRESENT
    rc = mm_camera_util_bundled_sendmsg(my_obj,
            &packet, sizeof(cam_sock_packet_t),
            sendfds, numbufs);
#else
    cam_shim_packet_t *shim_cmd;
    shim_cmd = mm_camera_create_shim_cmd_packet(CAM_SHIM_REG_BUF,
            my_obj->sessionid, &packet);
    rc = mm_camera_module_send_cmd(shim_cmd);
    mm_camera_destroy_shim_cmd_packet(shim_cmd);
#endif

    pthread_mutex_unlock(&my_obj->cam_lock);
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_unmap_buf
 *
 * DESCRIPTION: unmapping camera buffer via domain socket to server
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *   @buf_type     : type of buffer to be mapped. could be following values:
 *                   CAM_MAPPING_BUF_TYPE_CAPABILITY
 *                   CAM_MAPPING_BUF_TYPE_SETPARM_BUF
 *                   CAM_MAPPING_BUF_TYPE_GETPARM_BUF
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_camera_unmap_buf(mm_camera_obj_t *my_obj,
                            uint8_t buf_type)
{
    int32_t rc = 0;
    cam_sock_packet_t packet;
    memset(&packet, 0, sizeof(cam_sock_packet_t));
    packet.msg_type = CAM_MAPPING_TYPE_FD_UNMAPPING;
    packet.payload.buf_unmap.type = buf_type;
#ifdef DAEMON_PRESENT
    rc = mm_camera_util_sendmsg(my_obj,
                                &packet,
                                sizeof(cam_sock_packet_t),
                                -1);
#else
    cam_shim_packet_t *shim_cmd;
    shim_cmd = mm_camera_create_shim_cmd_packet(CAM_SHIM_REG_BUF,
            my_obj->sessionid, &packet);
    rc = mm_camera_module_send_cmd(shim_cmd);
    mm_camera_destroy_shim_cmd_packet(shim_cmd);
#endif
    pthread_mutex_unlock(&my_obj->cam_lock);
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_util_s_ctrl
 *
 * DESCRIPTION: utility function to send v4l2 ioctl for s_ctrl
 *
 * PARAMETERS :
 *   @my_obj     :Camera object
 *   @stream_id :streamID
 *   @fd      : file descritpor for sending ioctl
 *   @id      : control id
 *   @value   : value of the ioctl to be sent
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_camera_util_s_ctrl(__unused mm_camera_obj_t *my_obj,
        __unused int stream_id, int32_t fd,
        uint32_t id, int32_t *value)
{
    int rc = 0;

#ifdef DAEMON_PRESENT
    struct v4l2_control control;
    memset(&control, 0, sizeof(control));
    control.id = id;
    if (value != NULL) {
        control.value = *value;
    }
    rc = ioctl(fd, VIDIOC_S_CTRL, &control);
    LOGD("fd=%d, S_CTRL, id=0x%x, value = %p, rc = %d\n",
          fd, id, value, rc);
    if (rc < 0) {
        LOGE("ioctl failed %d, errno %d", rc, errno);
    } else if (value != NULL) {
        *value = control.value;
    }
#else /* DAEMON_PRESENT */
    cam_shim_packet_t *shim_cmd;
    cam_shim_cmd_data shim_cmd_data;
    (void)fd;
    (void)value;
    memset(&shim_cmd_data, 0, sizeof(shim_cmd_data));

    shim_cmd_data.command = id;
    shim_cmd_data.stream_id = stream_id;
    shim_cmd_data.value = NULL;
    shim_cmd = mm_camera_create_shim_cmd_packet(CAM_SHIM_SET_PARM,
            my_obj->sessionid,&shim_cmd_data);
    rc = mm_camera_module_send_cmd(shim_cmd);
    mm_camera_destroy_shim_cmd_packet(shim_cmd);
#endif /* DAEMON_PRESENT */
    return (rc >= 0)? 0 : -1;
}

/*===========================================================================
 * FUNCTION   : mm_camera_util_g_ctrl
 *
 * DESCRIPTION: utility function to send v4l2 ioctl for g_ctrl
 *
 * PARAMETERS :
 *   @my_obj     :Camera object
 *   @stream_id :streamID
 *   @fd      : file descritpor for sending ioctl
 *   @id      : control id
 *   @value   : value of the ioctl to be sent
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_camera_util_g_ctrl(__unused mm_camera_obj_t *my_obj,
        __unused int stream_id, int32_t fd, uint32_t id, int32_t *value)
{
    int rc = 0;
    struct v4l2_control control;

    memset(&control, 0, sizeof(control));
    control.id = id;
    if (value != NULL) {
        control.value = *value;
    }

#ifdef DAEMON_PRESENT
    rc = ioctl(fd, VIDIOC_G_CTRL, &control);
    LOGD("fd=%d, G_CTRL, id=0x%x, rc = %d\n", fd, id, rc);
    if (value != NULL) {
        *value = control.value;
    }
#else /* DAEMON_PRESENT */
    cam_shim_packet_t *shim_cmd;
    cam_shim_cmd_data shim_cmd_data;
    (void)fd;
    memset(&shim_cmd_data, 0, sizeof(shim_cmd_data));

    shim_cmd_data.command = id;
    shim_cmd_data.stream_id = stream_id;
    shim_cmd_data.value = value;
    shim_cmd = mm_camera_create_shim_cmd_packet(CAM_SHIM_GET_PARM,
            my_obj->sessionid, &shim_cmd_data);

    rc = mm_camera_module_send_cmd(shim_cmd);
    mm_camera_destroy_shim_cmd_packet(shim_cmd);
#endif /* DAEMON_PRESENT */
    return (rc >= 0)? 0 : -1;
}

/*===========================================================================
 * FUNCTION   : mm_camera_create_shim_cmd
 *
 * DESCRIPTION: Prepare comand packet to pass to back-end through shim layer
 *
 * PARAMETERS :
 *   @type                : type of command
 *   @sessionID        : camera sessionID
  *  @data                : command data
 *
 * RETURN     : NULL in case of failures
                      allocated pointer to shim packet
 *==========================================================================*/
cam_shim_packet_t *mm_camera_create_shim_cmd_packet(cam_shim_cmd_type type,
        uint32_t sessionID, void *data)
{
    cam_shim_packet_t *shim_pack = NULL;
    uint32_t i = 0;

    shim_pack = (cam_shim_packet_t *)malloc(sizeof(cam_shim_packet_t));
    if (shim_pack == NULL) {
        LOGE("Cannot allocate a memory for shim packet");
        return NULL;
    }
    memset(shim_pack, 0, sizeof(cam_shim_packet_t));
    shim_pack->cmd_type = type;
    shim_pack->session_id = sessionID;
    switch (type) {
        case CAM_SHIM_SET_PARM:
        case CAM_SHIM_GET_PARM: {
            cam_shim_cmd_data *cmd_data = (cam_shim_cmd_data *)data;
            shim_pack->cmd_data = *cmd_data;
            break;
        }
        case CAM_SHIM_REG_BUF: {
            cam_reg_buf_t *cmd_data = (cam_reg_buf_t *)data;
            shim_pack->reg_buf = *cmd_data;
            break;
        }
        case CAM_SHIM_BUNDLE_CMD: {
            cam_shim_stream_cmd_packet_t *cmd_data = (cam_shim_stream_cmd_packet_t *)data;
            for (i = 0; i < cmd_data->stream_count; i++) {
                shim_pack->bundle_cmd.stream_event[i] = cmd_data->stream_event[i];
            }
            shim_pack->bundle_cmd.stream_count = cmd_data->stream_count;
            break;
        }
        default:
            LOGW("No Data for this command");
    }
    return shim_pack;
}

/*===========================================================================
 * FUNCTION   : mm_camera_destroy_shim_cmd
 *
 * DESCRIPTION: destroy shim packet
 *
 * PARAMETERS :
 *   @cmd                : ptr to shim packet

 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_camera_destroy_shim_cmd_packet(cam_shim_packet_t *cmd)
{
    int32_t rc = 0;
    uint32_t i = 0, j = 0;

    if (cmd == NULL) {
        LOGW("Command is NULL");
        return rc;
    }

    switch (cmd->cmd_type) {
        case CAM_SHIM_SET_PARM:
        case CAM_SHIM_GET_PARM:
        case CAM_SHIM_REG_BUF:
            break;
        case CAM_SHIM_BUNDLE_CMD: {
            cam_shim_stream_cmd_packet_t *cmd_data = (cam_shim_stream_cmd_packet_t *)cmd;
            for (i = 0; i < cmd_data->stream_count; i++) {
                cam_shim_cmd_packet_t *stream_evt = &cmd_data->stream_event[i];
                for (j = 0; j < stream_evt->cmd_count; j++) {
                    if (stream_evt->cmd != NULL) {
                        if(stream_evt->cmd->cmd_type == CAM_SHIM_BUNDLE_CMD) {
                            mm_camera_destroy_shim_cmd_packet(stream_evt->cmd);
                        }
                        free(stream_evt->cmd);
                        stream_evt->cmd = NULL;
                    }
                }
            }
            break;
        }
        default:
            LOGW("No Data for this command");
    }
    free(cmd);
    cmd = NULL;
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_channel_advanced_capture
 *
 * DESCRIPTION: sets the channel advanced capture
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *   @ch_id        : channel handle
  *   @type : advanced capture type.
 *   @start_flag  : flag to indicate start/stop
  *   @in_value  : input configaration
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_camera_channel_advanced_capture(mm_camera_obj_t *my_obj,
            uint32_t ch_id, mm_camera_advanced_capture_t type,
            uint32_t trigger, void *in_value)
{
    LOGD("E type = %d", type);
    int32_t rc = -1;
    mm_channel_t * ch_obj =
        mm_camera_util_get_channel_by_handler(my_obj, ch_id);

    if (NULL != ch_obj) {
        pthread_mutex_lock(&ch_obj->ch_lock);
        pthread_mutex_unlock(&my_obj->cam_lock);
        switch (type) {
            case MM_CAMERA_AF_BRACKETING:
                rc = mm_channel_fsm_fn(ch_obj,
                                       MM_CHANNEL_EVT_AF_BRACKETING,
                                       (void *)&trigger,
                                       NULL);
                break;
            case MM_CAMERA_AE_BRACKETING:
                rc = mm_channel_fsm_fn(ch_obj,
                                       MM_CHANNEL_EVT_AE_BRACKETING,
                                       (void *)&trigger,
                                       NULL);
                break;
            case MM_CAMERA_FLASH_BRACKETING:
                rc = mm_channel_fsm_fn(ch_obj,
                                       MM_CHANNEL_EVT_FLASH_BRACKETING,
                                       (void *)&trigger,
                                       NULL);
                break;
            case MM_CAMERA_ZOOM_1X:
                rc = mm_channel_fsm_fn(ch_obj,
                                       MM_CHANNEL_EVT_ZOOM_1X,
                                       (void *)&trigger,
                                       NULL);
                break;
            case MM_CAMERA_FRAME_CAPTURE:
                rc = mm_channel_fsm_fn(ch_obj,
                                       MM_CAMERA_EVT_CAPTURE_SETTING,
                                       (void *)in_value,
                                       NULL);
                break;
            default:
                break;
        }

    } else {
        pthread_mutex_unlock(&my_obj->cam_lock);
    }

    LOGD("X");
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_get_session_id
 *
 * DESCRIPTION: get the session identity
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *   @sessionid: pointer to the output session id
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 * NOTE       : if this call succeeds, we will get a valid session id
 *==========================================================================*/
int32_t mm_camera_get_session_id(mm_camera_obj_t *my_obj,
        uint32_t* sessionid)
{
    int32_t rc = -1;
    int32_t value = 0;
    if(sessionid != NULL) {
        struct v4l2_control control;
        memset(&control, 0, sizeof(control));
        control.id = MSM_CAMERA_PRIV_G_SESSION_ID;
        control.value = value;

        rc = ioctl(my_obj->ctrl_fd, VIDIOC_G_CTRL, &control);
        value = control.value;
        LOGD("fd=%d, get_session_id, id=0x%x, value = %d, rc = %d\n",
                 my_obj->ctrl_fd, MSM_CAMERA_PRIV_G_SESSION_ID,
                value, rc);
        *sessionid = value;
    }
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_sync_related_sensors
 *
 * DESCRIPTION: send sync cmd
 *
 * PARAMETERS :
 *   @my_obj       : camera object
 *   @parms        : ptr to the related cam info to be sent to server
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 * NOTE       : Assume the sync struct buf is already mapped to server via
 *              domain socket. Corresponding fields of parameters to be set
 *              are already filled in by upper layer caller.
 *==========================================================================*/
int32_t mm_camera_sync_related_sensors(mm_camera_obj_t *my_obj,
        cam_sync_related_sensors_event_info_t* parms)
{
    int32_t rc = -1;
    int32_t value = 0;
    if (parms !=  NULL) {
        rc = mm_camera_util_s_ctrl(my_obj, 0, my_obj->ctrl_fd,
                CAM_PRIV_SYNC_RELATED_SENSORS, &value);
    }
    pthread_mutex_unlock(&my_obj->cam_lock);
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_reg_stream_buf_cb
 *
 * DESCRIPTION: Register callback for stream buffer
 *
 * PARAMETERS :
 *   @my_obj    : camera object
 *   @ch_id     : channel handle
 *   @stream_id : stream that will be linked
 *   @buf_cb    : special callback needs to be registered for stream buffer
 *   @cb_type   : Callback type SYNC/ASYNC
 *   @userdata  : user data pointer
 *
 * RETURN    : int32_t type of status
 *             0  -- success
 *             1 --  failure
 *==========================================================================*/
int32_t mm_camera_reg_stream_buf_cb(mm_camera_obj_t *my_obj,
        uint32_t ch_id, uint32_t stream_id, mm_camera_buf_notify_t stream_cb,
        mm_camera_stream_cb_type cb_type, void *userdata)
{
    int rc = 0;
    mm_stream_data_cb_t buf_cb;
    mm_channel_t * ch_obj =
            mm_camera_util_get_channel_by_handler(my_obj, ch_id);

    if (NULL != ch_obj) {
        pthread_mutex_lock(&ch_obj->ch_lock);
        pthread_mutex_unlock(&my_obj->cam_lock);

        memset(&buf_cb, 0, sizeof(mm_stream_data_cb_t));
        buf_cb.cb = stream_cb;
        buf_cb.cb_count = -1;
        buf_cb.cb_type = cb_type;
        buf_cb.user_data = userdata;

        mm_evt_paylod_reg_stream_buf_cb payload;
        memset(&payload, 0, sizeof(mm_evt_paylod_reg_stream_buf_cb));
        payload.buf_cb = buf_cb;
        payload.stream_id = stream_id;
        mm_channel_fsm_fn(ch_obj,
                MM_CHANNEL_EVT_REG_STREAM_BUF_CB,
                (void*)&payload, NULL);
    } else {
        pthread_mutex_unlock(&my_obj->cam_lock);
    }
    return rc;
}

#ifdef QCAMERA_REDEFINE_LOG

/*===========================================================================
 * DESCRIPTION: mm camera debug interface
 *
 *==========================================================================*/
pthread_mutex_t dbg_log_mutex;

#undef LOG_TAG
#define LOG_TAG "QCamera"
#define CDBG_MAX_STR_LEN 1024
#define CDBG_MAX_LINE_LENGTH 256

/* current trace loggin permissions
   * {NONE, ERR, WARN, HIGH, DEBUG, LOW, INFO} */
int g_cam_log[CAM_LAST_MODULE][CAM_GLBL_DBG_INFO + 1] = {
    {0, 1, 0, 0, 0, 0, 1}, /* CAM_NO_MODULE     */
    {0, 1, 0, 0, 0, 0, 1}, /* CAM_HAL_MODULE    */
    {0, 1, 0, 0, 0, 0, 1}, /* CAM_MCI_MODULE    */
    {0, 1, 0, 0, 0, 0, 1}, /* CAM_JPEG_MODULE   */
};

/* string representation for logging level */
static const char *cam_dbg_level_to_str[] = {
     "",        /* CAM_GLBL_DBG_NONE  */
     "<ERROR>", /* CAM_GLBL_DBG_ERR   */
     "<WARN>", /* CAM_GLBL_DBG_WARN  */
     "<HIGH>", /* CAM_GLBL_DBG_HIGH  */
     "<DBG>", /* CAM_GLBL_DBG_DEBUG */
     "<LOW>", /* CAM_GLBL_DBG_LOW   */
     "<INFO>"  /* CAM_GLBL_DBG_INFO  */
};

/* current trace logging configuration */
typedef struct {
   cam_global_debug_level_t  level;
   int                       initialized;
   const char               *name;
   const char               *prop;
} module_debug_t;

static module_debug_t cam_loginfo[(int)CAM_LAST_MODULE] = {
  {CAM_GLBL_DBG_ERR, 1,
      "",         "persist.camera.global.debug"     }, /* CAM_NO_MODULE     */
  {CAM_GLBL_DBG_ERR, 1,
      "<HAL>", "persist.camera.hal.debug"        }, /* CAM_HAL_MODULE    */
  {CAM_GLBL_DBG_ERR, 1,
      "<MCI>", "persist.camera.mci.debug"        }, /* CAM_MCI_MODULE    */
  {CAM_GLBL_DBG_ERR, 1,
      "<JPEG>", "persist.camera.mmstill.logs"     }, /* CAM_JPEG_MODULE   */
};

/** cam_get_dbg_level
 *
 *    @module: module name
 *    @level:  module debug logging level
 *
 *  Maps debug log string to value.
 *
 *  Return: logging level
 **/
__unused
static cam_global_debug_level_t cam_get_dbg_level(const char *module,
  char *pValue) {

  cam_global_debug_level_t rc = CAM_GLBL_DBG_NONE;

  if (!strcmp(pValue, "none")) {
    rc = CAM_GLBL_DBG_NONE;
  } else if (!strcmp(pValue, "warn")) {
    rc = CAM_GLBL_DBG_WARN;
  } else if (!strcmp(pValue, "debug")) {
    rc = CAM_GLBL_DBG_DEBUG;
  } else if (!strcmp(pValue, "error")) {
    rc = CAM_GLBL_DBG_ERR;
  } else if (!strcmp(pValue, "low")) {
    rc = CAM_GLBL_DBG_LOW;
  } else if (!strcmp(pValue, "high")) {
    rc = CAM_GLBL_DBG_HIGH;
  } else if (!strcmp(pValue, "info")) {
    rc = CAM_GLBL_DBG_INFO;
  } else {
    ALOGE("Invalid %s debug log level %s\n", module, pValue);
  }

  ALOGD("%s debug log level: %s\n", module, cam_dbg_level_to_str[rc]);

  return rc;
}

/** cam_vsnprintf
 *    @pdst:   destination buffer pointer
 *    @size:   size of destination b uffer
 *    @pfmt:   string format
 *    @argptr: variabkle length argument list
 *
 *  Processes variable length argument list to a formatted string.
 *
 *  Return: n/a
 **/
static void cam_vsnprintf(char* pdst, unsigned int size,
                          const char* pfmt, va_list argptr) {
  int num_chars_written = 0;

  pdst[0] = '\0';
  num_chars_written = vsnprintf(pdst, size, pfmt, argptr);

  if ((num_chars_written >= (int)size) && (size > 0)) {
     /* Message length exceeds the buffer limit size */
     num_chars_written = size - 1;
     pdst[size - 1] = '\0';
  }
}

/** mm_camera_debug_log
 *    @module: origin or log message
 *    @level:  logging level
 *    @func:   caller function name
 *    @line:   caller line number
 *    @fmt:    log message formatting string
 *    @...:    variable argument list
 *
 *  Generig logger method.
 *
 *  Return: N/A
 **/
void mm_camera_debug_log(const cam_modules_t module,
                   const cam_global_debug_level_t level,
                   const char *func, const int line, const char *fmt, ...) {
  char    str_buffer[CDBG_MAX_STR_LEN];
  va_list args;

  va_start(args, fmt);
  cam_vsnprintf(str_buffer, CDBG_MAX_STR_LEN, fmt, args);
  va_end(args);

  switch (level) {
  case CAM_GLBL_DBG_WARN:
    ALOGW("%s%s %s: %d: %s", cam_loginfo[module].name,
      cam_dbg_level_to_str[level], func, line, str_buffer);
    break;
  case CAM_GLBL_DBG_ERR:
    ALOGE("%s%s %s: %d: %s", cam_loginfo[module].name,
      cam_dbg_level_to_str[level], func, line, str_buffer);
    break;
  case CAM_GLBL_DBG_INFO:
    ALOGI("%s%s %s: %d: %s", cam_loginfo[module].name,
      cam_dbg_level_to_str[level], func, line, str_buffer);
    break;
  case CAM_GLBL_DBG_HIGH:
  case CAM_GLBL_DBG_DEBUG:
  case CAM_GLBL_DBG_LOW:
  default:
    ALOGD("%s%s %s: %d: %s", cam_loginfo[module].name,
      cam_dbg_level_to_str[level], func, line, str_buffer);
  }
}

 /** mm_camera_set_dbg_log_properties
 *
 *  Set global and module log level properties.
 *
 *  Return: N/A
 **/
void mm_camera_set_dbg_log_properties(void) {
  int          i;
  unsigned int j;
  static int   boot_init = 1;
  char         property_value[PROPERTY_VALUE_MAX] = {0};
  char         default_value[PROPERTY_VALUE_MAX]  = {0};

  if (boot_init) {
      boot_init = 0;
      pthread_mutex_init(&dbg_log_mutex, 0);
  }

  /* set global and individual module logging levels */
  pthread_mutex_lock(&dbg_log_mutex);
  for (i = CAM_NO_MODULE; i < CAM_LAST_MODULE; i++) {
    cam_global_debug_level_t log_level;
    snprintf(default_value, PROPERTY_VALUE_MAX, "%d", (int)cam_loginfo[i].level);
    property_get(cam_loginfo[i].prop, property_value, default_value);
    log_level = (cam_global_debug_level_t)atoi(property_value);

    /* fix KW warnings */
    if (log_level > CAM_GLBL_DBG_INFO) {
       log_level = CAM_GLBL_DBG_INFO;
    }

    cam_loginfo[i].level = log_level;

    /* The logging macros will produce a log message when logging level for
     * a module is less or equal to the level specified in the property for
     * the module, or less or equal the level specified by the global logging
     * property. Currently we don't allow INFO logging to be turned off */
    for (j = CAM_GLBL_DBG_ERR; j <= CAM_GLBL_DBG_LOW; j++) {
      g_cam_log[i][j] = (cam_loginfo[CAM_NO_MODULE].level != CAM_GLBL_DBG_NONE)     &&
                        (cam_loginfo[i].level             != CAM_GLBL_DBG_NONE)     &&
                        ((j                                <= cam_loginfo[i].level) ||
                         (j                                <= cam_loginfo[CAM_NO_MODULE].level));
    }
  }
  pthread_mutex_unlock(&dbg_log_mutex);
}

#endif
