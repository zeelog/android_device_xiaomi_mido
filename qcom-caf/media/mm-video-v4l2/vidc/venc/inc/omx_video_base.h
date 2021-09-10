/*--------------------------------------------------------------------------
Copyright (c) 2010-2017, The Linux Foundation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name of The Linux Foundation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
--------------------------------------------------------------------------*/

#ifndef __OMX_VIDEO_BASE_H__
#define __OMX_VIDEO_BASE_H__
/*============================================================================
                            O p e n M A X   Component
                                Video Encoder

*//** @file comx_video_base.h
  This module contains the class definition for openMAX decoder component.

*//*========================================================================*/

//////////////////////////////////////////////////////////////////////////////
//                             Include Files
//////////////////////////////////////////////////////////////////////////////

#define LOG_TAG "OMX-VENC"
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#ifdef _ANDROID_
#ifdef _ANDROID_ICS_
#include "QComOMXMetadata.h"
#endif
#endif // _ANDROID_
#include <pthread.h>
#include <semaphore.h>
#ifndef _TARGET_KERNEL_VERSION_49_
#include <linux/msm_vidc_enc.h>
#endif
#include <media/hardware/HardwareAPI.h>
#include "OMX_Core.h"
#include "OMX_QCOMExtns.h"
#include "OMX_Skype_VideoExtensions.h"
#include "OMX_VideoExt.h"
#include "OMX_IndexExt.h"
#include "qc_omx_component.h"
#include "omx_video_common.h"
#include "extra_data_handler.h"
#include <linux/videodev2.h>
#include <dlfcn.h>
#include "C2DColorConverter.h"
#include "vidc_debug.h"
#include <vector>
#include "vidc_vendor_extensions.h"

#ifdef _ANDROID_
using namespace android;
#include <utils/Log.h>

#endif // _ANDROID_

#ifdef USE_ION
static const char* MEM_DEVICE = "/dev/ion";
#if defined(MAX_RES_720P) && !defined(_MSM8974_)
#define MEM_HEAP_ID ION_CAMERA_HEAP_ID
#else
#ifdef _MSM8974_
#define MEM_HEAP_ID ION_IOMMU_HEAP_ID
#else
#define MEM_HEAP_ID ION_CP_MM_HEAP_ID
#endif
#endif
#elif MAX_RES_720P
static const char* MEM_DEVICE = "/dev/pmem_adsp";
#elif MAX_RES_1080P_EBI
static const char* MEM_DEVICE  = "/dev/pmem_adsp";
#elif MAX_RES_1080P
static const char* MEM_DEVICE = "/dev/pmem_smipool";
#else
#error MEM_DEVICE cannot be determined.
#endif

//////////////////////////////////////////////////////////////////////////////
//                       Module specific globals
//////////////////////////////////////////////////////////////////////////////
#define OMX_SPEC_VERSION 0x00000101
#define OMX_INIT_STRUCT(_s_, _name_)            \
    memset((_s_), 0x0, sizeof(_name_));          \
(_s_)->nSize = sizeof(_name_);               \
(_s_)->nVersion.nVersion = OMX_SPEC_VERSION

//////////////////////////////////////////////////////////////////////////////
//               Macros
//////////////////////////////////////////////////////////////////////////////
#define PrintFrameHdr(bufHdr) DEBUG_PRINT("bufHdr %x buf %x size %d TS %d\n",\
        (unsigned) bufHdr,\
        (unsigned)((OMX_BUFFERHEADERTYPE *)bufHdr)->pBuffer,\
        (unsigned)((OMX_BUFFERHEADERTYPE *)bufHdr)->nFilledLen,\
        (unsigned)((OMX_BUFFERHEADERTYPE *)bufHdr)->nTimeStamp)

// BitMask Management logic
#define BITS_PER_INDEX        64
#define BITMASK_SIZE(mIndex) (((mIndex) + BITS_PER_INDEX - 1)/BITS_PER_INDEX)
#define BITMASK_OFFSET(mIndex) ((mIndex)/BITS_PER_INDEX)
#define BITMASK_FLAG(mIndex) ((uint64_t)1 << ((mIndex) % BITS_PER_INDEX))
#define BITMASK_CLEAR(mArray,mIndex) (mArray)[BITMASK_OFFSET(mIndex)] \
    &=  ~(BITMASK_FLAG(mIndex))
#define BITMASK_SET(mArray,mIndex)  (mArray)[BITMASK_OFFSET(mIndex)] \
    |=  BITMASK_FLAG(mIndex)
#define BITMASK_PRESENT(mArray,mIndex) ((mArray)[BITMASK_OFFSET(mIndex)] \
        & BITMASK_FLAG(mIndex))
#define BITMASK_ABSENT(mArray,mIndex) (((mArray)[BITMASK_OFFSET(mIndex)] \
            & BITMASK_FLAG(mIndex)) == 0x0)
#define BITMASK_PRESENT(mArray,mIndex) ((mArray)[BITMASK_OFFSET(mIndex)] \
        & BITMASK_FLAG(mIndex))
#define BITMASK_ABSENT(mArray,mIndex) (((mArray)[BITMASK_OFFSET(mIndex)] \
            & BITMASK_FLAG(mIndex)) == 0x0)

#define MAX_NUM_INPUT_BUFFERS 64
#define MAX_NUM_OUTPUT_BUFFERS 64

#ifdef USE_NATIVE_HANDLE_SOURCE
#define LEGACY_CAM_SOURCE kMetadataBufferTypeNativeHandleSource
#define LEGACY_CAM_METADATA_TYPE encoder_nativehandle_buffer_type
#else
#define LEGACY_CAM_SOURCE kMetadataBufferTypeCameraSource
#define LEGACY_CAM_METADATA_TYPE encoder_media_buffer_type
#endif

#ifdef _TARGET_KERNEL_VERSION_49_
/** ENCODER STATUS CODES*/
/* Base value for status codes */
#define VEN_S_BASE    0x00000000
#define VEN_S_SUCCESS    (VEN_S_BASE)/* Success */
#define VEN_S_EFAIL    (VEN_S_BASE+1)/* General failure */

/*Asynchronous messages from driver*/
#define VEN_MSG_INDICATION    0
#define VEN_MSG_INPUT_BUFFER_DONE    1
#define VEN_MSG_OUTPUT_BUFFER_DONE    2
#define VEN_MSG_NEED_OUTPUT_BUFFER    3
#define VEN_MSG_FLUSH_INPUT_DONE    4
#define VEN_MSG_FLUSH_OUTPUT_DONE    5
#define VEN_MSG_START    6
#define VEN_MSG_STOP    7
#define VEN_MSG_PAUSE    8
#define VEN_MSG_RESUME    9
#define VEN_MSG_LTRUSE_FAILED    10
#define VEN_MSG_HW_OVERLOAD    11
#define VEN_MSG_MAX_CLIENTS    12

/*Video codec profile types.*/
#define VEN_PROFILE_MPEG4_SP      1/* 1 - MPEG4 SP profile      */
#define VEN_PROFILE_MPEG4_ASP     2/* 2 - MPEG4 ASP profile     */
#define VEN_PROFILE_H264_BASELINE 3/* 3 - H264 Baseline profile */
#define VEN_PROFILE_H264_MAIN     4/* 4 - H264 Main profile     */
#define VEN_PROFILE_H264_HIGH     5/* 5 - H264 High profile     */
#define VEN_PROFILE_H263_BASELINE 6/* 6 - H263 Baseline profile */

/*Different methods of Multi slice selection.*/
#define VEN_MSLICE_OFF    1
#define VEN_MSLICE_CNT_MB    2 /*number of MBscount per slice*/
#define VEN_MSLICE_CNT_BYTE    3 /*number of bytes count per slice.*/
#define VEN_MSLICE_GOB    4 /*Multi slice by GOB for H.263 only.*/

/*Video codec profile level types.*/
#define VEN_LEVEL_MPEG4_0    0x1/* MPEG4 Level 0  */
#define VEN_LEVEL_MPEG4_1    0x2/* MPEG4 Level 1  */
#define VEN_LEVEL_MPEG4_2    0x3/* MPEG4 Level 2  */
#define VEN_LEVEL_MPEG4_3    0x4/* MPEG4 Level 3  */
#define VEN_LEVEL_MPEG4_4    0x5/* MPEG4 Level 4  */
#define VEN_LEVEL_MPEG4_5    0x6/* MPEG4 Level 5  */
#define VEN_LEVEL_MPEG4_3b    0x7/* MPEG4 Level 3b */
#define VEN_LEVEL_MPEG4_6    0x8/* MPEG4 Level 6  */

#define VEN_LEVEL_H264_1    0x9/* H.264 Level 1   */
#define VEN_LEVEL_H264_1b    0xA/* H.264 Level 1b  */
#define VEN_LEVEL_H264_1p1    0xB/* H.264 Level 1.1 */
#define VEN_LEVEL_H264_1p2    0xC/* H.264 Level 1.2 */
#define VEN_LEVEL_H264_1p3    0xD/* H.264 Level 1.3 */
#define VEN_LEVEL_H264_2    0xE/* H.264 Level 2   */
#define VEN_LEVEL_H264_2p1    0xF/* H.264 Level 2.1 */
#define VEN_LEVEL_H264_2p2    0x10/* H.264 Level 2.2 */
#define VEN_LEVEL_H264_3    0x11/* H.264 Level 3   */
#define VEN_LEVEL_H264_3p1    0x12/* H.264 Level 3.1 */
#define VEN_LEVEL_H264_3p2    0x13/* H.264 Level 3.2 */
#define VEN_LEVEL_H264_4    0x14/* H.264 Level 4   */

#define VEN_LEVEL_H263_10    0x15/* H.263 Level 10  */
#define VEN_LEVEL_H263_20    0x16/* H.263 Level 20  */
#define VEN_LEVEL_H263_30    0x17/* H.263 Level 30  */
#define VEN_LEVEL_H263_40    0x18/* H.263 Level 40  */
#define VEN_LEVEL_H263_45    0x19/* H.263 Level 45  */
#define VEN_LEVEL_H263_50    0x1A/* H.263 Level 50  */
#define VEN_LEVEL_H263_60    0x1B/* H.263 Level 60  */
#define VEN_LEVEL_H263_70    0x1C/* H.263 Level 70  */
#endif //_TARGET_KERNEL_VERSION_49_

class omx_video;
void post_message(omx_video *omx, unsigned char id);
void* message_thread_enc(void *);

enum omx_venc_extradata_types {
    VENC_EXTRADATA_SLICEINFO = 0x100,
    VENC_EXTRADATA_LTRINFO = 0x200,
    VENC_EXTRADATA_MBINFO = 0x400,
    VENC_EXTRADATA_FRAMEDIMENSION = 0x1000000,
    VENC_EXTRADATA_YUV_STATS = 0x800,
    VENC_EXTRADATA_VQZIP = 0x02000000,
    VENC_EXTRADATA_ROI = 0x04000000,
};

struct output_metabuffer {
    OMX_U32 type;
    native_handle_t *nh;
};

#ifdef _TARGET_KERNEL_VERSION_49_
struct venc_buffer{
    unsigned char *ptrbuffer;
    unsigned long    sz;
    unsigned long    len;
    unsigned long    offset;
    long long    timestamp;
    unsigned long    flags;
    void    *clientdata;
};

struct venc_bufferpayload{
    unsigned char *pbuffer;
    size_t    sz;
    int    fd;
    unsigned int    offset;
    unsigned int    maped_size;
    unsigned long    filled_len;
};

struct venc_voptimingcfg{
    unsigned long    voptime_resolution;
};

struct venc_framerate{
    unsigned long    fps_denominator;
    unsigned long    fps_numerator;
};

struct venc_headerextension{
    unsigned long    header_extension;
};

struct venc_multiclicecfg{
    unsigned long    mslice_mode;
    unsigned long    mslice_size;
};

struct venc_msg{
    unsigned long    statuscode;
    unsigned long    msgcode;
    struct venc_buffer    buf;
    unsigned long    msgdata_size;
};

struct venc_profile {
    unsigned long    profile;
};

struct ven_profilelevel {
    unsigned long    level;
};
#endif //_TARGET_KERNEL_VERSION_49_

typedef struct encoder_meta_buffer_payload_type {
    char data[sizeof(LEGACY_CAM_METADATA_TYPE) + sizeof(int)];
} encoder_meta_buffer_payload_type;

// OMX video class
class omx_video: public qc_omx_component
{
    protected:
#ifdef _ANDROID_ICS_
        bool meta_mode_enable;
        bool c2d_opened;
        encoder_meta_buffer_payload_type meta_buffers[MAX_NUM_INPUT_BUFFERS];
        OMX_BUFFERHEADERTYPE *opaque_buffer_hdr[MAX_NUM_INPUT_BUFFERS];
        bool get_syntaxhdr_enable;
        OMX_BUFFERHEADERTYPE  *psource_frame;
        OMX_BUFFERHEADERTYPE  *pdest_frame;
        bool secure_session;
        bool hier_b_enabled;
        //intermediate conversion buffer queued to encoder in case of invalid EOS input
        OMX_BUFFERHEADERTYPE  *mEmptyEosBuffer;

        class omx_c2d_conv
        {
            public:
                omx_c2d_conv();
                ~omx_c2d_conv();
                bool init();
                bool open(unsigned int height,unsigned int width,
                        ColorConvertFormat src, ColorConvertFormat dest,
                        unsigned int src_stride, unsigned int flags);
                bool convert(int src_fd, void *src_base, void *src_viraddr,
                        int dest_fd, void *dest_base, void *dest_viraddr);
                bool get_buffer_size(int port,unsigned int &buf_size);
                int get_src_format();
                void close();
            private:
                C2DColorConverterBase *c2dcc;
                pthread_mutex_t c_lock;
                void *mLibHandle;
                ColorConvertFormat src_format;
                createC2DColorConverter_t *mConvertOpen;
                destroyC2DColorConverter_t *mConvertClose;
        };
        omx_c2d_conv c2d_conv;
#endif
    public:

        bool mUseProxyColorFormat;
        //RGB or non-native input, and we have pre-allocated conversion buffers
        bool mUsesColorConversion;

        omx_video();  // constructor
        virtual ~omx_video();  // destructor

        // virtual int async_message_process (void *context, void* message);
        void process_event_cb(void *ctxt);

        OMX_ERRORTYPE allocate_buffer(
                OMX_HANDLETYPE hComp,
                OMX_BUFFERHEADERTYPE **bufferHdr,
                OMX_U32 port,
                OMX_PTR appData,
                OMX_U32 bytes
                );


        virtual OMX_ERRORTYPE component_deinit(OMX_HANDLETYPE hComp)= 0;

        virtual OMX_ERRORTYPE component_init(OMX_STRING role)= 0;

        virtual OMX_U32 dev_stop(void) = 0;
        virtual OMX_U32 dev_pause(void) = 0;
        virtual OMX_U32 dev_start(void) = 0;
        virtual OMX_U32 dev_flush(unsigned) = 0;
        virtual OMX_U32 dev_resume(void) = 0;
        virtual OMX_U32 dev_start_done(void) = 0;
        virtual OMX_U32 dev_set_message_thread_id(pthread_t) = 0;
        virtual bool dev_handle_empty_eos_buffer(void) = 0;
        virtual bool dev_use_buf(void *,unsigned,unsigned) = 0;
        virtual bool dev_free_buf(void *,unsigned) = 0;
        virtual bool dev_empty_buf(void *, void *,unsigned,unsigned) = 0;
        virtual bool dev_fill_buf(void *buffer, void *,unsigned,unsigned) = 0;
        virtual bool dev_get_buf_req(OMX_U32 *,OMX_U32 *,OMX_U32 *,OMX_U32) = 0;
        virtual bool dev_get_dimensions(OMX_U32 ,OMX_U32 *,OMX_U32 *) = 0;
        virtual bool dev_get_seq_hdr(void *, unsigned, unsigned *) = 0;
        virtual bool dev_loaded_start(void) = 0;
        virtual bool dev_loaded_stop(void) = 0;
        virtual bool dev_loaded_start_done(void) = 0;
        virtual bool dev_loaded_stop_done(void) = 0;
        virtual bool is_secure_session(void) = 0;
        virtual int dev_handle_output_extradata(void*, int) = 0;
        virtual int dev_set_format(int) = 0;
        virtual bool dev_is_video_session_supported(OMX_U32 width, OMX_U32 height) = 0;
        virtual bool dev_get_capability_ltrcount(OMX_U32 *, OMX_U32 *, OMX_U32 *) = 0;
        virtual bool dev_get_performance_level(OMX_U32 *) = 0;
        virtual bool dev_get_vui_timing_info(OMX_U32 *) = 0;
        virtual bool dev_get_vqzip_sei_info(OMX_U32 *) = 0;
        virtual bool dev_get_peak_bitrate(OMX_U32 *) = 0;
        virtual bool dev_get_batch_size(OMX_U32 *) = 0;
        virtual bool dev_buffer_ready_to_queue(OMX_BUFFERHEADERTYPE *buffer) = 0;
        virtual bool dev_get_temporal_layer_caps(OMX_U32 * /*nMaxLayers*/,
                OMX_U32 * /*nMaxBLayers*/) = 0;
        virtual bool dev_get_pq_status(OMX_BOOL *) = 0;
#ifdef _ANDROID_ICS_
        void omx_release_meta_buffer(OMX_BUFFERHEADERTYPE *buffer);
#endif
        virtual bool dev_color_align(OMX_BUFFERHEADERTYPE *buffer, OMX_U32 width,
                        OMX_U32 height) = 0;
        virtual bool dev_get_output_log_flag() = 0;
        virtual int dev_output_log_buffers(const char *buffer_addr, int buffer_len, uint64_t timestamp) = 0;
        virtual int dev_extradata_log_buffers(char *buffer_addr) = 0;
        OMX_ERRORTYPE component_role_enum(
                OMX_HANDLETYPE hComp,
                OMX_U8 *role,
                OMX_U32 index
                );

        OMX_ERRORTYPE component_tunnel_request(
                OMX_HANDLETYPE hComp,
                OMX_U32 port,
                OMX_HANDLETYPE  peerComponent,
                OMX_U32 peerPort,
                OMX_TUNNELSETUPTYPE *tunnelSetup
                );

        OMX_ERRORTYPE empty_this_buffer(
                OMX_HANDLETYPE hComp,
                OMX_BUFFERHEADERTYPE *buffer
                );



        OMX_ERRORTYPE fill_this_buffer(
                OMX_HANDLETYPE hComp,
                OMX_BUFFERHEADERTYPE *buffer
                );


        OMX_ERRORTYPE free_buffer(
                OMX_HANDLETYPE hComp,
                OMX_U32 port,
                OMX_BUFFERHEADERTYPE *buffer
                );

        OMX_ERRORTYPE get_component_version(
                OMX_HANDLETYPE hComp,
                OMX_STRING componentName,
                OMX_VERSIONTYPE *componentVersion,
                OMX_VERSIONTYPE *specVersion,
                OMX_UUIDTYPE *componentUUID
                );

        OMX_ERRORTYPE get_config(
                OMX_HANDLETYPE hComp,
                OMX_INDEXTYPE configIndex,
                OMX_PTR configData
                );

        OMX_ERRORTYPE get_extension_index(
                OMX_HANDLETYPE hComp,
                OMX_STRING paramName,
                OMX_INDEXTYPE *indexType
                );

        OMX_ERRORTYPE get_parameter(OMX_HANDLETYPE hComp,
                OMX_INDEXTYPE  paramIndex,
                OMX_PTR        paramData);

        OMX_ERRORTYPE get_state(OMX_HANDLETYPE hComp,
                OMX_STATETYPE *state);



        OMX_ERRORTYPE send_command(OMX_HANDLETYPE  hComp,
                OMX_COMMANDTYPE cmd,
                OMX_U32         param1,
                OMX_PTR         cmdData);


        OMX_ERRORTYPE set_callbacks(OMX_HANDLETYPE   hComp,
                OMX_CALLBACKTYPE *callbacks,
                OMX_PTR          appData);

        virtual OMX_ERRORTYPE set_config(OMX_HANDLETYPE hComp,
                OMX_INDEXTYPE  configIndex,
                OMX_PTR        configData) = 0;

        virtual OMX_ERRORTYPE set_parameter(OMX_HANDLETYPE hComp,
                OMX_INDEXTYPE  paramIndex,
                OMX_PTR        paramData) =0;

        OMX_ERRORTYPE use_buffer(OMX_HANDLETYPE      hComp,
                OMX_BUFFERHEADERTYPE **bufferHdr,
                OMX_U32              port,
                OMX_PTR              appData,
                OMX_U32              bytes,
                OMX_U8               *buffer);


        OMX_ERRORTYPE use_EGL_image(OMX_HANDLETYPE     hComp,
                OMX_BUFFERHEADERTYPE **bufferHdr,
                OMX_U32              port,
                OMX_PTR              appData,
                void *               eglImage);

        Signal signal;
        int  m_pipe_in;
        int  m_pipe_out;
        pthread_t msg_thread_id;
        pthread_t async_thread_id;
        bool async_thread_created;
        bool msg_thread_created;
        volatile bool msg_thread_stop;

        OMX_U8 m_nkind[128];


        //int *input_pmem_fd;
        //int *output_pmem_fd;
        struct pmem *m_pInput_pmem;
        struct pmem *m_pOutput_pmem;
#ifdef USE_ION
        struct venc_ion *m_pInput_ion;
        struct venc_ion *m_pOutput_ion;
#endif



    public:
        // Bit Positions
        enum flags_bit_positions {
            // Defer transition to IDLE
            OMX_COMPONENT_IDLE_PENDING            =0x1,
            // Defer transition to LOADING
            OMX_COMPONENT_LOADING_PENDING         =0x2,
            // First  Buffer Pending
            OMX_COMPONENT_FIRST_BUFFER_PENDING    =0x3,
            // Second Buffer Pending
            OMX_COMPONENT_SECOND_BUFFER_PENDING   =0x4,
            // Defer transition to Enable
            OMX_COMPONENT_INPUT_ENABLE_PENDING    =0x5,
            // Defer transition to Enable
            OMX_COMPONENT_OUTPUT_ENABLE_PENDING   =0x6,
            // Defer transition to Disable
            OMX_COMPONENT_INPUT_DISABLE_PENDING   =0x7,
            // Defer transition to Disable
            OMX_COMPONENT_OUTPUT_DISABLE_PENDING  =0x8,
            //defer flush notification
            OMX_COMPONENT_OUTPUT_FLUSH_PENDING    =0x9,
            OMX_COMPONENT_INPUT_FLUSH_PENDING    =0xA,
            OMX_COMPONENT_PAUSE_PENDING          =0xB,
            OMX_COMPONENT_EXECUTE_PENDING        =0xC,
            OMX_COMPONENT_LOADED_START_PENDING = 0xD,
            OMX_COMPONENT_LOADED_STOP_PENDING = 0xF,

        };

        // Deferred callback identifiers
        enum {
            //Event Callbacks from the venc component thread context
            OMX_COMPONENT_GENERATE_EVENT       = 0x1,
            //Buffer Done callbacks from the venc component thread context
            OMX_COMPONENT_GENERATE_BUFFER_DONE = 0x2,
            //Frame Done callbacks from the venc component thread context
            OMX_COMPONENT_GENERATE_FRAME_DONE  = 0x3,
            //Buffer Done callbacks from the venc component thread context
            OMX_COMPONENT_GENERATE_FTB         = 0x4,
            //Frame Done callbacks from the venc component thread context
            OMX_COMPONENT_GENERATE_ETB         = 0x5,
            //Command
            OMX_COMPONENT_GENERATE_COMMAND     = 0x6,
            //Push-Pending Buffers
            OMX_COMPONENT_PUSH_PENDING_BUFS    = 0x7,
            // Empty Buffer Done callbacks
            OMX_COMPONENT_GENERATE_EBD         = 0x8,
            //Flush Event Callbacks from the venc component thread context
            OMX_COMPONENT_GENERATE_EVENT_FLUSH       = 0x9,
            OMX_COMPONENT_GENERATE_EVENT_INPUT_FLUSH = 0x0A,
            OMX_COMPONENT_GENERATE_EVENT_OUTPUT_FLUSH = 0x0B,
            OMX_COMPONENT_GENERATE_FBD = 0xc,
            OMX_COMPONENT_GENERATE_START_DONE = 0xD,
            OMX_COMPONENT_GENERATE_PAUSE_DONE = 0xE,
            OMX_COMPONENT_GENERATE_RESUME_DONE = 0xF,
            OMX_COMPONENT_GENERATE_STOP_DONE = 0x10,
            OMX_COMPONENT_GENERATE_HARDWARE_ERROR = 0x11,
            OMX_COMPONENT_GENERATE_LTRUSE_FAILED = 0x12,
            OMX_COMPONENT_GENERATE_ETB_OPQ = 0x13,
            OMX_COMPONENT_GENERATE_UNSUPPORTED_SETTING = 0x14,
            OMX_COMPONENT_GENERATE_HARDWARE_OVERLOAD = 0x15,
            OMX_COMPONENT_CLOSE_MSG = 0x16
        };

        struct omx_event {
            unsigned long param1;
            unsigned long param2;
            unsigned long id;
        };

        struct omx_cmd_queue {
            omx_event m_q[OMX_CORE_CONTROL_CMDQ_SIZE];
            unsigned long m_read;
            unsigned long m_write;
            unsigned long m_size;

            omx_cmd_queue();
            ~omx_cmd_queue();
            bool insert_entry(unsigned long p1, unsigned long p2, unsigned long id);
            bool pop_entry(unsigned long *p1,unsigned long *p2, unsigned long *id);
            // get msgtype of the first ele from the queue
            unsigned get_q_msg_type();

        };

        bool allocate_done(void);
        bool allocate_input_done(void);
        bool allocate_output_done(void);

        OMX_ERRORTYPE free_input_buffer(OMX_BUFFERHEADERTYPE *bufferHdr);
        OMX_ERRORTYPE free_output_buffer(OMX_BUFFERHEADERTYPE *bufferHdr);

        OMX_ERRORTYPE allocate_input_buffer(OMX_HANDLETYPE       hComp,
                OMX_BUFFERHEADERTYPE **bufferHdr,
                OMX_U32              port,
                OMX_PTR              appData,
                OMX_U32              bytes);
#ifdef _ANDROID_ICS_
        OMX_ERRORTYPE allocate_input_meta_buffer(OMX_HANDLETYPE       hComp,
                OMX_BUFFERHEADERTYPE **bufferHdr,
                OMX_PTR              appData,
                OMX_U32              bytes);
#endif
        OMX_ERRORTYPE allocate_output_buffer(OMX_HANDLETYPE       hComp,
                OMX_BUFFERHEADERTYPE **bufferHdr,
                OMX_U32 port,OMX_PTR appData,
                OMX_U32              bytes);

        OMX_ERRORTYPE use_input_buffer(OMX_HANDLETYPE hComp,
                OMX_BUFFERHEADERTYPE  **bufferHdr,
                OMX_U32               port,
                OMX_PTR               appData,
                OMX_U32               bytes,
                OMX_U8                *buffer);

        OMX_ERRORTYPE use_output_buffer(OMX_HANDLETYPE hComp,
                OMX_BUFFERHEADERTYPE   **bufferHdr,
                OMX_U32                port,
                OMX_PTR                appData,
                OMX_U32                bytes,
                OMX_U8                 *buffer);

        bool execute_omx_flush(OMX_U32);
        bool execute_output_flush(void);
        bool execute_input_flush(void);
#ifdef _MSM8974_
        bool execute_flush_all(void);
#endif
        OMX_ERRORTYPE empty_buffer_done(OMX_HANDLETYPE hComp,
                OMX_BUFFERHEADERTYPE * buffer);

        OMX_ERRORTYPE fill_buffer_done(OMX_HANDLETYPE hComp,
                OMX_BUFFERHEADERTYPE * buffer);
        OMX_ERRORTYPE empty_this_buffer_proxy(OMX_HANDLETYPE hComp,
                OMX_BUFFERHEADERTYPE *buffer);
        OMX_ERRORTYPE empty_this_buffer_opaque(OMX_HANDLETYPE hComp,
                OMX_BUFFERHEADERTYPE *buffer);
        OMX_ERRORTYPE push_input_buffer(OMX_HANDLETYPE hComp);
        OMX_ERRORTYPE convert_queue_buffer(OMX_HANDLETYPE hComp,
                struct pmem &Input_pmem_info,unsigned long &index);
        OMX_ERRORTYPE queue_meta_buffer(OMX_HANDLETYPE hComp,
                struct pmem &Input_pmem_info);
        OMX_ERRORTYPE push_empty_eos_buffer(OMX_HANDLETYPE hComp);
        OMX_ERRORTYPE fill_this_buffer_proxy(OMX_HANDLETYPE hComp,
                OMX_BUFFERHEADERTYPE *buffer);
        bool release_done();

        bool release_output_done();
        bool release_input_done();

        OMX_ERRORTYPE send_command_proxy(OMX_HANDLETYPE  hComp,
                OMX_COMMANDTYPE cmd,
                OMX_U32         param1,
                OMX_PTR         cmdData);
        bool post_event( unsigned long p1,
                unsigned long p2,
                unsigned long id
                   );
        OMX_ERRORTYPE get_supported_profile_level(OMX_VIDEO_PARAM_PROFILELEVELTYPE *profileLevelType);
        inline void omx_report_error () {
            if (m_pCallbacks.EventHandler && !m_error_propogated && m_state != OMX_StateLoaded) {
                m_error_propogated = true;
                DEBUG_PRINT_ERROR("ERROR: send OMX_ErrorHardware to Client");
                m_pCallbacks.EventHandler(&m_cmp,m_app_data,
                        OMX_EventError,OMX_ErrorHardware,0,NULL);
            }
        }

        inline void omx_report_hw_overload ()
        {
            if (m_pCallbacks.EventHandler && !m_error_propogated && m_state != OMX_StateLoaded) {
                m_error_propogated = true;
                DEBUG_PRINT_ERROR("ERROR: send OMX_ErrorInsufficientResources to Client");
                m_pCallbacks.EventHandler(&m_cmp, m_app_data,
                        OMX_EventError, OMX_ErrorInsufficientResources, 0, NULL);
            }
        }

        inline void omx_report_unsupported_setting () {
            if (m_pCallbacks.EventHandler && !m_error_propogated && m_state != OMX_StateLoaded) {
                m_error_propogated = true;
                m_pCallbacks.EventHandler(&m_cmp,m_app_data,
                        OMX_EventError,OMX_ErrorUnsupportedSetting,0,NULL);
            }
        }

        void complete_pending_buffer_done_cbs();
        bool is_conv_needed(int, int);
        void print_debug_color_aspects(ColorAspects *aspects, const char *prefix);

        OMX_ERRORTYPE get_vendor_extension_config(
                OMX_CONFIG_ANDROID_VENDOR_EXTENSIONTYPE *ext);
        OMX_ERRORTYPE set_vendor_extension_config(
                OMX_CONFIG_ANDROID_VENDOR_EXTENSIONTYPE *ext);
        void init_vendor_extensions(VendorExtensionStore&);
        // Extensions-store is immutable after initialization (i.e cannot add/remove/change
        //  extensions once added !)
        const VendorExtensionStore mVendorExtensionStore;

#ifdef USE_ION
        int alloc_map_ion_memory(int size,
                                 struct ion_allocation_data *alloc_data,
                                 struct ion_fd_data *fd_data,int flag);
        void free_ion_memory(struct venc_ion *buf_ion_info);
#endif

        //*************************************************************
        //*******************MEMBER VARIABLES *************************
        //*************************************************************

        pthread_mutex_t       m_lock;
        sem_t                 m_cmd_lock;
        bool              m_error_propogated;

        //sem to handle the minimum procesing of commands


        // compression format
        //OMX_VIDEO_CODINGTYPE eCompressionFormat;
        // OMX State
        OMX_STATETYPE m_state;
        // Application data
        OMX_PTR m_app_data;
        OMX_BOOL m_use_input_pmem;
        OMX_BOOL m_use_output_pmem;
        // Application callbacks
        OMX_CALLBACKTYPE m_pCallbacks;
        OMX_PORT_PARAM_TYPE m_sPortParam;
        OMX_VIDEO_PARAM_PROFILELEVELTYPE m_sParamProfileLevel;
        OMX_VIDEO_PARAM_PORTFORMATTYPE m_sInPortFormat;
        OMX_VIDEO_PARAM_PORTFORMATTYPE m_sOutPortFormat;
        OMX_PARAM_PORTDEFINITIONTYPE m_sInPortDef;
        OMX_PARAM_PORTDEFINITIONTYPE m_sOutPortDef;
        OMX_VIDEO_PARAM_MPEG4TYPE m_sParamMPEG4;
        OMX_VIDEO_PARAM_H263TYPE m_sParamH263;
        OMX_VIDEO_PARAM_AVCTYPE m_sParamAVC;
        OMX_VIDEO_PARAM_VP8TYPE m_sParamVP8;
        OMX_VIDEO_PARAM_HEVCTYPE m_sParamHEVC;
        OMX_PORT_PARAM_TYPE m_sPortParam_img;
        OMX_PORT_PARAM_TYPE m_sPortParam_audio;
        OMX_VIDEO_CONFIG_BITRATETYPE m_sConfigBitrate;
        OMX_CONFIG_FRAMERATETYPE m_sConfigFramerate;
        OMX_VIDEO_PARAM_BITRATETYPE m_sParamBitrate;
        OMX_PRIORITYMGMTTYPE m_sPriorityMgmt;
        OMX_PARAM_BUFFERSUPPLIERTYPE m_sInBufSupplier;
        OMX_PARAM_BUFFERSUPPLIERTYPE m_sOutBufSupplier;
        OMX_CONFIG_ROTATIONTYPE m_sConfigFrameRotation;
        OMX_CONFIG_INTRAREFRESHVOPTYPE m_sConfigIntraRefreshVOP;
        OMX_VIDEO_PARAM_QUANTIZATIONTYPE m_sSessionQuantization;
        OMX_QCOM_VIDEO_PARAM_QPRANGETYPE m_sSessionQPRange;
        OMX_QCOM_VIDEO_PARAM_IPB_QPRANGETYPE m_sSessionIPBQPRange;
        OMX_VIDEO_PARAM_AVCSLICEFMO m_sAVCSliceFMO;
        QOMX_VIDEO_INTRAPERIODTYPE m_sIntraperiod;
        OMX_VIDEO_PARAM_ERRORCORRECTIONTYPE m_sErrorCorrection;
        QOMX_VIDEO_PARAM_SLICE_SPACING_TYPE m_sSliceSpacing;
        OMX_VIDEO_PARAM_INTRAREFRESHTYPE m_sIntraRefresh;
        QOMX_VIDEO_PARAM_LTRMODE_TYPE m_sParamLTRMode;
        QOMX_VIDEO_PARAM_LTRCOUNT_TYPE m_sParamLTRCount;
        QOMX_VIDEO_CONFIG_LTRPERIOD_TYPE m_sConfigLTRPeriod;
        QOMX_VIDEO_CONFIG_LTRUSE_TYPE m_sConfigLTRUse;
        OMX_VIDEO_CONFIG_AVCINTRAPERIOD m_sConfigAVCIDRPeriod;
        OMX_VIDEO_CONFIG_DEINTERLACE m_sConfigDeinterlace;
        OMX_VIDEO_VP8REFERENCEFRAMETYPE m_sConfigVp8ReferenceFrame;
        QOMX_VIDEO_HIERARCHICALLAYERS m_sHierLayers;
        OMX_QOMX_VIDEO_MBI_STATISTICS m_sMBIStatistics;
        QOMX_EXTNINDEX_VIDEO_INITIALQP m_sParamInitqp;
        QOMX_EXTNINDEX_VIDEO_HIER_P_LAYERS m_sHPlayers;
        OMX_SKYPE_VIDEO_CONFIG_BASELAYERPID m_sBaseLayerID;
        OMX_SKYPE_VIDEO_PARAM_DRIVERVER m_sDriverVer;
        OMX_SKYPE_VIDEO_CONFIG_QP m_sConfigQP;
        QOMX_EXTNINDEX_VIDEO_VENC_SAR m_sSar;
        QOMX_VIDEO_H264ENTROPYCODINGTYPE m_sParamEntropy;
        PrependSPSPPSToIDRFramesParams m_sPrependSPSPPS;
        struct timestamp_info {
            OMX_S64 ts;
            omx_cmd_queue deferred_inbufq;
            pthread_mutex_t m_lock;
        } m_TimeStampInfo;
        OMX_U32 m_sExtraData;
        OMX_U32 m_input_msg_id;
        QOMX_EXTNINDEX_VIDEO_VENC_LOW_LATENCY_MODE m_slowLatencyMode;
#ifdef SUPPORT_CONFIG_INTRA_REFRESH
        OMX_VIDEO_CONFIG_ANDROID_INTRAREFRESHTYPE m_sConfigIntraRefresh;
#endif
        OMX_QTI_VIDEO_CONFIG_BLURINFO       m_blurInfo;
        DescribeColorAspectsParams m_sConfigColorAspects;
        OMX_VIDEO_PARAM_ANDROID_TEMPORALLAYERINGTYPE m_sParamTemporalLayers;
        OMX_VIDEO_CONFIG_ANDROID_TEMPORALLAYERINGTYPE m_sConfigTemporalLayers;
        QOMX_ENABLETYPE m_sParamAVTimerTimestampMode;   // use VT-timestamps in gralloc-handle
        QOMX_ENABLETYPE m_sParamControlInputQueue;
        OMX_TIME_CONFIG_TIMESTAMPTYPE m_sConfigInputTrigTS;

        // fill this buffer queue
        omx_cmd_queue m_ftb_q;
        // Command Q for rest of the events
        omx_cmd_queue m_cmd_q;
        omx_cmd_queue m_etb_q;
        // Input memory pointer
        OMX_BUFFERHEADERTYPE *m_inp_mem_ptr;
        // Output memory pointer
        OMX_BUFFERHEADERTYPE *m_out_mem_ptr;
        omx_cmd_queue m_opq_meta_q;
        omx_cmd_queue m_opq_pmem_q;
        OMX_BUFFERHEADERTYPE meta_buffer_hdr[MAX_NUM_INPUT_BUFFERS];
        pthread_mutex_t m_buf_lock;

        bool input_flush_progress;
        bool output_flush_progress;
        bool input_use_buffer;
        bool output_use_buffer;
        int pending_input_buffers;
        int pending_output_buffers;

        bool allocate_native_handle;

        uint64_t m_out_bm_count;
        uint64_t m_client_out_bm_count;
        uint64_t m_client_in_bm_count;
        uint64_t m_inp_bm_count;
        uint64_t m_flags;
        uint64_t m_etb_count;
        uint64_t m_fbd_count;
        OMX_TICKS m_etb_timestamp;
        // to know whether Event Port Settings change has been triggered or not.
        bool m_event_port_settings_sent;
        OMX_U8                m_cRole[OMX_MAX_STRINGNAME_SIZE];
        extra_data_handler extra_data_handle;
        bool hw_overload;
        size_t m_graphicbuffer_size;
        char m_platform[OMX_MAX_STRINGNAME_SIZE];
		bool m_buffer_freed;
};

#endif // __OMX_VIDEO_BASE_H__
