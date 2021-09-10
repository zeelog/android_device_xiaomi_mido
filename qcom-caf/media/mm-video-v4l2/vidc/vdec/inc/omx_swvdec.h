/**
 * @copyright
 *
 *   Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *   * Neither the name of The Linux Foundation nor the names of its
 *     contributors may be used to endorse or promote products derived from
 *     this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *   INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY,
 *   FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE DISCLAIMED.
 *   IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY
 *   DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *   (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *   CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *   LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *   OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 *   DAMAGE.
 *
 * @file
 *
 *   omx_swvdec.h
 *
 * @brief
 *
 *   OMX software video decoder component header.
 */

#ifndef _OMX_SWVDEC_H_
#define _OMX_SWVDEC_H_

//#undef NDEBUG // uncomment to enable assertions

#include <pthread.h>
#include <semaphore.h>

#include <linux/msm_ion.h>
#ifndef _TARGET_KERNEL_VERSION_49_
#include <linux/msm_vidc_dec.h>
#endif
#include "qc_omx_component.h"

#include "omx_swvdec_utils.h"

#include "swvdec_types.h"

using namespace android;

/// OMX SwVdec version date
#define OMX_SWVDEC_VERSION_DATE "2017-10-06T11:32:51+0530"

#define OMX_SPEC_VERSION 0x00000101 ///< OMX specification version

#define OMX_SWVDEC_NUM_INSTANCES 1 ///< number of OMX SwVdec instances

#define OMX_SWVDEC_IP_BUFFER_COUNT_MIN 5 ///< OMX SwVdec minimum ip buffer count

#define OMX_SWVDEC_MAX_FRAMES_PER_ETB 2 ///< maximum number of frames per ETB

/// frame dimensions structure
typedef struct {
    unsigned int width;  ///< frame width
    unsigned int height; ///< frame height
} FRAME_DIMENSIONS;

/// frame attributes structure
typedef struct {
    unsigned int stride;    ///< frame stride
    unsigned int scanlines; ///< frame scanlines
    unsigned int size;      ///< frame size
} FRAME_ATTRIBUTES;

/// asynchronous thread structure
typedef struct {
    sem_t     sem_thread_created; ///< thread created semaphore
    sem_t     sem_event;          ///< event semaphore
    pthread_t handle;             ///< thread handle
    bool      created;            ///< thread created?
    bool      exit;               ///< thread exit variable
} ASYNC_THREAD;

/// @cond
#ifdef _TARGET_KERNEL_VERSION_49_
struct vdec_bufferpayload {
    void *bufferaddr;
    size_t buffer_len;
    int pmem_fd;
    size_t offset;
    size_t mmaped_size;
};
#endif //_TARGET_KERNEL_VERSION_49_
struct vdec_ion {
    int                        ion_fd_device;
    struct ion_fd_data         ion_fd_data;
    struct ion_allocation_data ion_alloc_data;
};

typedef struct {
    OMX_BUFFERHEADERTYPE      buffer_header;
    struct vdec_ion           ion_info;
    struct vdec_bufferpayload buffer_payload;
    SWVDEC_BUFFER             buffer_swvdec;
    bool                      buffer_populated;
    unsigned int              split_count;
} OMX_SWVDEC_BUFFER_INFO;

/// @endcond

/// port structure
typedef struct {
    OMX_PARAM_PORTDEFINITIONTYPE def;                 ///< definition
    OMX_BOOL                     enabled;             ///< enabled?
    OMX_BOOL                     populated;           ///< populated?
    OMX_BOOL                     unpopulated;         ///< unpopulated?
    OMX_BOOL                     flush_inprogress;    ///< flush inprogress?
    unsigned int                 num_pending_buffers; ///< # of pending buffers
} OMX_SWVDEC_PORT;

/// meta_buffer information structure
typedef struct {
    int fd;        ///< file descriptor
    int ref_count; ///< reference count
} OMX_SWVDEC_META_BUFFER_INFO;

#define DEFAULT_FRAME_WIDTH  1920 ///< default frame width
#define DEFAULT_FRAME_HEIGHT 1080 ///< default frame height

#define MAX(x, y) (((x) > (y)) ? (x) : (y)) ///< maximum
#define MIN(x, y) (((x) < (y)) ? (x) : (y)) ///< minimum
#define ALIGN(x, y) (((x) + ((y) - 1)) & (~((y) - 1)))
                                  ///< align 'x' to next highest multiple of 'y'

/// macro to print 'command type' string
#define OMX_COMMANDTYPE_STRING(x)                                 \
    ((x == OMX_CommandStateSet) ? "OMX_CommandStateSet" :         \
     ((x == OMX_CommandFlush) ? "OMX_CommandFlush" :              \
      ((x == OMX_CommandPortDisable) ? "OMX_CommandPortDisable" : \
       ((x == OMX_CommandPortEnable) ? "OMX_CommandPortEnable" :  \
        "unknown"))))

/// macro to print 'state type' string
#define OMX_STATETYPE_STRING(x)                                            \
    ((x == OMX_StateInvalid) ? "OMX_StateInvalid" :                        \
     ((x == OMX_StateLoaded) ? "OMX_StateLoaded" :                         \
      ((x == OMX_StateIdle) ? "OMX_StateIdle" :                            \
       ((x == OMX_StateExecuting) ? "OMX_StateExecuting" :                 \
        ((x == OMX_StatePause) ? "OMX_StatePause" :                        \
         ((x == OMX_StateWaitForResources) ? "OMX_StateWaitForResources" : \
          "unknown"))))))

enum {
    OMX_CORE_PORT_INDEX_IP = 0, ///<  input port index
    OMX_CORE_PORT_INDEX_OP = 1  ///< output port index
};

extern "C" {
    OMX_API void *get_omx_component_factory_fn(void);
};

/// OMX SwVdec component class; derived from QC OMX component base class
class omx_swvdec : public qc_omx_component
{
public:

    omx_swvdec();

    virtual ~omx_swvdec();

    // derived class versions of base class pure virtual functions

    OMX_ERRORTYPE component_init(OMX_STRING cmp_name);
    OMX_ERRORTYPE component_deinit(OMX_HANDLETYPE cmp_handle);
    OMX_ERRORTYPE get_component_version(OMX_HANDLETYPE   cmp_handle,
                                        OMX_STRING       cmp_name,
                                        OMX_VERSIONTYPE *p_cmp_version,
                                        OMX_VERSIONTYPE *p_spec_version,
                                        OMX_UUIDTYPE    *p_cmp_UUID);
    OMX_ERRORTYPE send_command(OMX_HANDLETYPE  cmp_handle,
                               OMX_COMMANDTYPE cmd,
                               OMX_U32         param,
                               OMX_PTR         p_cmd_data);
    OMX_ERRORTYPE get_parameter(OMX_HANDLETYPE cmp_handle,
                                OMX_INDEXTYPE  param_index,
                                OMX_PTR        p_param_data);
    OMX_ERRORTYPE set_parameter(OMX_HANDLETYPE cmp_handle,
                                OMX_INDEXTYPE  param_index,
                                OMX_PTR        p_param_data);
    OMX_ERRORTYPE get_config(OMX_HANDLETYPE cmp_handle,
                             OMX_INDEXTYPE  config_index,
                             OMX_PTR        p_config_data);
    OMX_ERRORTYPE set_config(OMX_HANDLETYPE cmp_handle,
                             OMX_INDEXTYPE  config_index,
                             OMX_PTR        p_config_data);
    OMX_ERRORTYPE get_extension_index(OMX_HANDLETYPE cmp_handle,
                                      OMX_STRING     param_name,
                                      OMX_INDEXTYPE *p_index_type);
    OMX_ERRORTYPE get_state(OMX_HANDLETYPE cmp_handle,
                            OMX_STATETYPE *p_state);
    OMX_ERRORTYPE component_tunnel_request(OMX_HANDLETYPE       cmp_handle,
                                           OMX_U32              port,
                                           OMX_HANDLETYPE       peer_component,
                                           OMX_U32              peer_port,
                                           OMX_TUNNELSETUPTYPE *p_tunnel_setup);
    OMX_ERRORTYPE use_buffer(OMX_HANDLETYPE         cmp_handle,
                             OMX_BUFFERHEADERTYPE **pp_buffer_hdr,
                             OMX_U32                port,
                             OMX_PTR                p_app_data,
                             OMX_U32                bytes,
                             OMX_U8                *p_buffer);
    OMX_ERRORTYPE allocate_buffer(OMX_HANDLETYPE         cmp_handle,
                                  OMX_BUFFERHEADERTYPE **pp_buffer_hdr,
                                  OMX_U32                port,
                                  OMX_PTR                p_app_data,
                                  OMX_U32                bytes);
    OMX_ERRORTYPE free_buffer(OMX_HANDLETYPE        cmp_handle,
                              OMX_U32               port,
                              OMX_BUFFERHEADERTYPE *p_buffer);
    OMX_ERRORTYPE empty_this_buffer(OMX_HANDLETYPE        cmp_handle,
                                    OMX_BUFFERHEADERTYPE *p_buffer_hdr);
    OMX_ERRORTYPE fill_this_buffer(OMX_HANDLETYPE        cmp_handle,
                                   OMX_BUFFERHEADERTYPE *p_buffer_hdr);
    OMX_ERRORTYPE set_callbacks(OMX_HANDLETYPE    cmp_handle,
                                OMX_CALLBACKTYPE *p_callbacks,
                                OMX_PTR           p_app_data);
    OMX_ERRORTYPE use_EGL_image(OMX_HANDLETYPE         cmp_handle,
                                OMX_BUFFERHEADERTYPE **pp_buffer_hdr,
                                OMX_U32                port,
                                OMX_PTR                p_app_data,
                                void                  *egl_image);
    OMX_ERRORTYPE component_role_enum(OMX_HANDLETYPE cmp_handle,
                                      OMX_U8        *p_role,
                                      OMX_U32        index);

    // SwVdec callback functions

    static SWVDEC_STATUS swvdec_empty_buffer_done_callback(
        SWVDEC_HANDLE  swvdec_handle,
        SWVDEC_BUFFER *p_buffer_ip,
        void          *p_client_handle);
    static SWVDEC_STATUS swvdec_fill_buffer_done_callback(
        SWVDEC_HANDLE  swvdec_handle,
        SWVDEC_BUFFER *p_buffer_op,
        void          *p_client_handle);
    static SWVDEC_STATUS swvdec_event_handler_callback(
        SWVDEC_HANDLE swvdec_handle,
        SWVDEC_EVENT  event,
        void         *p_data,
        void         *p_client_handle);

private:

    OMX_STATETYPE m_state; ///< component state

    unsigned int m_status_flags; ///< status flags

    char m_cmp_name[OMX_MAX_STRINGNAME_SIZE];  ///< component name
    char m_role_name[OMX_MAX_STRINGNAME_SIZE]; ///< component role name

    SWVDEC_CODEC  m_swvdec_codec;   ///< SwVdec codec type
    SWVDEC_HANDLE m_swvdec_handle;  ///< SwVdec handle
    bool          m_swvdec_created; ///< SwVdec created?

    OMX_VIDEO_CODINGTYPE m_omx_video_codingtype; ///< OMX video coding type
    OMX_COLOR_FORMATTYPE m_omx_color_formattype; ///< OMX color format type

    FRAME_DIMENSIONS m_frame_dimensions; ///< frame dimensions
    FRAME_ATTRIBUTES m_frame_attributes; ///< frame attributes

    FRAME_DIMENSIONS m_frame_dimensions_max;
                                 ///< max frame dimensions for adaptive playback

    ASYNC_THREAD m_async_thread; ///< asynchronous thread

    omx_swvdec_queue m_queue_command; ///< command queue
    omx_swvdec_queue m_queue_port_ip; ///<  input port queue for ETBs & EBDs
    omx_swvdec_queue m_queue_port_op; ///< output port queue for FTBs & FBDs

    OMX_SWVDEC_PORT m_port_ip; ///<  input port
    OMX_SWVDEC_PORT m_port_op; ///< output port

    OMX_CALLBACKTYPE m_callback; ///< IL client callback structure
    OMX_PTR          m_app_data; ///< IL client app data pointer

    OMX_PRIORITYMGMTTYPE m_prio_mgmt; ///< priority management

    bool m_sync_frame_decoding_mode; ///< sync frame decoding mode enabled?
    bool m_android_native_buffers;   ///< android native buffers enabled?

    bool m_meta_buffer_mode_disabled; ///< meta buffer mode disabled?
    bool m_meta_buffer_mode;          ///< meta buffer mode enabled?
    bool m_adaptive_playback_mode;    ///< adaptive playback mode enabled?
    bool m_arbitrary_bytes_mode;      ///< arbitrary bytes mode enabled?

    bool m_port_reconfig_inprogress; ///< port reconfiguration in progress?

    bool m_dimensions_update_inprogress; ///< dimensions update in progress?

    sem_t m_sem_cmd; ///< semaphore for command processing

    OMX_SWVDEC_BUFFER_INFO *m_buffer_array_ip; ///<  input buffer info array
    OMX_SWVDEC_BUFFER_INFO *m_buffer_array_op; ///< output buffer info array

    OMX_SWVDEC_META_BUFFER_INFO *m_meta_buffer_array; ///< metabuffer info array
    pthread_mutex_t              m_meta_buffer_array_mutex;
                                            ///< mutex for metabuffer info array

    std::priority_queue <OMX_TICKS,
                         std::vector<OMX_TICKS>,
                         std::greater<OMX_TICKS> > m_queue_timestamp;
                                                   ///< timestamp priority queue

    omx_swvdec_diag m_diag; ///< diagnostics class variable

    OMX_ERRORTYPE set_frame_dimensions(unsigned int width,
                                       unsigned int height);
    OMX_ERRORTYPE set_frame_attributes(OMX_COLOR_FORMATTYPE color_format);
    OMX_ERRORTYPE set_adaptive_playback(unsigned int max_width,
                                        unsigned int max_height);

    OMX_ERRORTYPE get_video_port_format(
        OMX_VIDEO_PARAM_PORTFORMATTYPE *p_port_format);
    OMX_ERRORTYPE set_video_port_format(
        OMX_VIDEO_PARAM_PORTFORMATTYPE *p_port_format);

    OMX_ERRORTYPE get_port_definition(OMX_PARAM_PORTDEFINITIONTYPE *p_port_def);
    OMX_ERRORTYPE set_port_definition(OMX_PARAM_PORTDEFINITIONTYPE *p_port_def);

    OMX_ERRORTYPE get_supported_profilelevel(
        OMX_VIDEO_PARAM_PROFILELEVELTYPE *p_profilelevel);

    OMX_ERRORTYPE describe_color_format(DescribeColorFormatParams *p_params);

    OMX_ERRORTYPE set_port_definition_qcom(
        OMX_QCOM_PARAM_PORTDEFINITIONTYPE *p_port_def);

    // functions to set SwVdec properties with OMX component properties

    OMX_ERRORTYPE set_frame_dimensions_swvdec();
    OMX_ERRORTYPE set_frame_attributes_swvdec();
    OMX_ERRORTYPE set_adaptive_playback_swvdec();

    // functions to get SwVdec properties and set OMX component properties

    OMX_ERRORTYPE get_frame_dimensions_swvdec();
    OMX_ERRORTYPE get_frame_attributes_swvdec();
    OMX_ERRORTYPE get_buffer_requirements_swvdec(unsigned int port_index);

    // buffer allocation & de-allocation functions
    OMX_ERRORTYPE buffer_allocate_ip(OMX_BUFFERHEADERTYPE **pp_buffer_hdr,
                                     OMX_PTR                p_app_data,
                                     OMX_U32                size);
    OMX_ERRORTYPE buffer_allocate_op(OMX_BUFFERHEADERTYPE **pp_buffer_hdr,
                                     OMX_PTR                p_app_data,
                                     OMX_U32                size);
    OMX_ERRORTYPE buffer_allocate_ip_info_array();
    OMX_ERRORTYPE buffer_allocate_op_info_array();
    OMX_ERRORTYPE buffer_use_op(OMX_BUFFERHEADERTYPE **pp_buffer_hdr,
                                OMX_PTR                p_app_data,
                                OMX_U32                size,
                                OMX_U8                *p_buffer);
    OMX_ERRORTYPE buffer_deallocate_ip(OMX_BUFFERHEADERTYPE *p_buffer_hdr);
    OMX_ERRORTYPE buffer_deallocate_op(OMX_BUFFERHEADERTYPE *p_buffer_hdr);
    void          buffer_deallocate_ip_info_array();
    void          buffer_deallocate_op_info_array();

    OMX_ERRORTYPE meta_buffer_array_allocate();
    void          meta_buffer_array_deallocate();
    void          meta_buffer_ref_add(unsigned int index, int fd);
    void          meta_buffer_ref_remove(unsigned int index);

    OMX_BOOL port_ip_populated();
    OMX_BOOL port_op_populated();

    OMX_ERRORTYPE flush(unsigned int port_index);

    int  ion_memory_alloc_map(struct ion_allocation_data *p_alloc_data,
                              struct ion_fd_data         *p_fd_data,
                              OMX_U32                     size,
                              OMX_U32                     alignment);
    void ion_memory_free(struct vdec_ion *p_ion_buf_info);
    void ion_flush_op(unsigned int index);

    // component callback functions

    void swvdec_empty_buffer_done(SWVDEC_BUFFER *p_buffer_ip);
    void swvdec_fill_buffer_done(SWVDEC_BUFFER *p_buffer_op);
    void swvdec_event_handler(SWVDEC_EVENT event, void *p_data);

    OMX_ERRORTYPE retval_swvdec2omx(SWVDEC_STATUS retval_swvdec);

    // status bits for pending events
    enum {
        PENDING_STATE_LOADED_TO_IDLE,    ///< loaded to idle state
        PENDING_STATE_EXECUTING_TO_IDLE, ///< executing to idle state
        PENDING_STATE_IDLE_TO_LOADED,    ///< idle to loaded state
        PENDING_PORT_ENABLE_IP,          ///< enablement of ip port
        PENDING_PORT_ENABLE_OP,          ///< enablement of op port
        PENDING_PORT_DISABLE_IP,         ///< disablement of ip port
        PENDING_PORT_DISABLE_OP,         ///< disablement of op port
        PENDING_PORT_FLUSH_IP,           ///< flush of ip port
        PENDING_PORT_FLUSH_OP            ///< flush of op port
    };

    // events raised internally
    enum {
        OMX_SWVDEC_EVENT_CMD,               ///< command event
        OMX_SWVDEC_EVENT_CMD_ACK,           ///< command acknowledgement
        OMX_SWVDEC_EVENT_ERROR,             ///< error event
        OMX_SWVDEC_EVENT_ETB,               ///< ETB event
        OMX_SWVDEC_EVENT_EBD,               ///< EBD event
        OMX_SWVDEC_EVENT_FTB,               ///< FTB event
        OMX_SWVDEC_EVENT_FBD,               ///< FBD event
        OMX_SWVDEC_EVENT_EOS,               ///< EOS event
        OMX_SWVDEC_EVENT_FLUSH_PORT_IP,     ///< flush ip port event
        OMX_SWVDEC_EVENT_FLUSH_PORT_OP,     ///< flush op port event
        OMX_SWVDEC_EVENT_PORT_RECONFIG,     ///< port reconfig event
        OMX_SWVDEC_EVENT_DIMENSIONS_UPDATED ///< dimensions updated event
    };

    OMX_ERRORTYPE async_thread_create();
    void          async_thread_destroy();

    static void   async_thread(void *p_cmp);

    void          async_post_event(unsigned long event_id,
                                   unsigned long event_param1,
                                   unsigned long event_param2);

    static void   async_process_event(void *p_cmp);

    OMX_ERRORTYPE async_process_event_cmd(OMX_COMMANDTYPE cmd, OMX_U32 param);
    OMX_ERRORTYPE async_process_event_cmd_ack(OMX_COMMANDTYPE cmd,
                                              OMX_U32         param);
    OMX_ERRORTYPE async_process_event_error(OMX_ERRORTYPE error_code);
    OMX_ERRORTYPE async_process_event_cmd_state_set(bool         *p_cmd_ack,
                                                    OMX_STATETYPE state_new);
    OMX_ERRORTYPE async_process_event_cmd_flush(unsigned int port_index);
    OMX_ERRORTYPE async_process_event_cmd_port_disable(
        bool         *p_cmd_ack,
        unsigned int  port_index);
    OMX_ERRORTYPE async_process_event_cmd_port_enable(bool        *p_cmd_ack,
                                                      unsigned int port_index);
    OMX_ERRORTYPE async_process_event_etb(OMX_BUFFERHEADERTYPE *p_buffer_hdr,
                                          unsigned int          index);
    OMX_ERRORTYPE async_process_event_ftb(OMX_BUFFERHEADERTYPE *p_buffer_hdr,
                                          unsigned int          index);
    OMX_ERRORTYPE async_process_event_ebd(OMX_BUFFERHEADERTYPE *p_buffer_hdr,
                                          unsigned int          index);
    OMX_ERRORTYPE async_process_event_fbd(OMX_BUFFERHEADERTYPE *p_buffer_hdr,
                                          unsigned int          index);
    OMX_ERRORTYPE async_process_event_eos();
    OMX_ERRORTYPE async_process_event_flush_port_ip();
    OMX_ERRORTYPE async_process_event_flush_port_op();
    OMX_ERRORTYPE async_process_event_port_reconfig();
    OMX_ERRORTYPE async_process_event_dimensions_updated();
};

#endif // #ifndef _OMX_SWVDEC_H_
