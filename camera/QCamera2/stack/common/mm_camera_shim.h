/* Copyright (c) 2016, The Linux Foundation. All rights reserved.
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

#ifndef __MM_CAMERA_SHIMLAYER_H_
#define __MM_CAMERA_SHIMLAYER_H_

#include "cam_intf.h"

/*
 * MCT shim layer APIs
 */
#define SHIMLAYER_LIB "/system/vendor/lib/libmmcamera2_mct_shimlayer.so"

struct cam_shim_packet;

/*
* Bundled events structure.
*/
typedef struct {
    uint8_t cmd_count;            /* Total number of events in this packet */
    struct cam_shim_packet *cmd;  /*Events to process*/
} cam_shim_cmd_packet_t;

/*
* Bundled stream event structure
*/
typedef struct {
    uint8_t stream_count;                                /*Number of streams in a bundle*/
    cam_shim_cmd_packet_t stream_event[MAX_NUM_STREAMS]; /*Event for different streams*/
} cam_shim_stream_cmd_packet_t;

/*
* Command types to process in shim layer
*/
typedef enum {
    CAM_SHIM_SET_PARM,   /*v4l2 set parameter*/
    CAM_SHIM_GET_PARM,   /*v4l2 get parameter*/
    CAM_SHIM_REG_BUF,    /*Reg/unreg buffers with back-end*/
    CAM_SHIM_BUNDLE_CMD, /*Bundled command for streams*/
} cam_shim_cmd_type;

typedef struct {
    uint32_t command;    /*V4L2 or private command*/
    uint32_t stream_id;  /*streamID*/
    void *value;          /*command value/data*/
} cam_shim_cmd_data;

/*
* Structure to communicate command with shim layer
*/
typedef struct cam_shim_packet {
    uint32_t session_id;
    cam_shim_cmd_type cmd_type;                 /*Command type to process*/
    union {
        cam_shim_cmd_data cmd_data;             /*get/set parameter structure*/
        cam_reg_buf_t reg_buf;                  /*Buffer register and unregister*/
        cam_shim_stream_cmd_packet_t bundle_cmd;/*Bundled command*/
    };
} cam_shim_packet_t;

typedef int32_t (*mm_camera_shim_event_handler_func)(uint32_t session_id,
        cam_event_t *event);

typedef struct {
    cam_status_t (*mm_camera_shim_open_session) (int session,
            mm_camera_shim_event_handler_func evt_cb);
    int32_t (*mm_camera_shim_close_session)(int session);
    int32_t (*mm_camera_shim_send_cmd)(cam_shim_packet_t *event);
} mm_camera_shim_ops_t;

int32_t (*mm_camera_shim_module_init)(mm_camera_shim_ops_t *shim_ops);

#endif  /*__MM_CAMERA_SHIMLAYER_H_*/
