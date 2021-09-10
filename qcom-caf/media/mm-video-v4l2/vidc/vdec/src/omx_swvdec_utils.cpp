/**
 * @copyright
 *
 *   Copyright (c) 2015-2017, The Linux Foundation. All rights reserved.
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
 *   omx_swvdec_utils.cpp
 *
 * @brief
 *
 *   OMX software video decoder utility functions source.
 */

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>

#include <cutils/properties.h>

#include "omx_swvdec_utils.h"

#define OMX_SWVDEC_LOGLEVEL_DEFAULT 2 ///< default OMX SwVdec loglevel

unsigned int g_omx_swvdec_logmask = (1 << OMX_SWVDEC_LOGLEVEL_DEFAULT) - 1;
                              ///< global OMX SwVdec logmask variable definition

/**
 * @brief Initialize OMX SwVdec log level & mask.
 */
void omx_swvdec_log_init()
{
    int omx_swvdec_loglevel = OMX_SWVDEC_LOGLEVEL_DEFAULT;

    char property_value[PROPERTY_VALUE_MAX] = {0};

    if (property_get("vendor.omx_swvdec.log.level", property_value, NULL))
    {
        omx_swvdec_loglevel = atoi(property_value);

        if (omx_swvdec_loglevel > 3)
        {
            omx_swvdec_loglevel = 3;
        }

        if (omx_swvdec_loglevel < 0)
        {
            omx_swvdec_loglevel = 0;
        }

        OMX_SWVDEC_LOG_HIGH(
            "vendor.omx_swvdec.log.level: %d; %s",
            omx_swvdec_loglevel,
            (omx_swvdec_loglevel == 3) ? "error, high, & low logs" :
            ((omx_swvdec_loglevel == 2) ? "error & high logs" :
             ((omx_swvdec_loglevel == 1) ? "error logs" :
              "no logs")));
    }

    g_omx_swvdec_logmask = (unsigned int) ((1 << omx_swvdec_loglevel) - 1);
}

/**
 * @brief OMX SwVdec queue constructor.
 */
omx_swvdec_queue::omx_swvdec_queue()
{
    pthread_mutex_init(&m_mutex, NULL);
}

/**
 * @brief OMX SwVdec queue destructor.
 */
omx_swvdec_queue::~omx_swvdec_queue()
{
    pthread_mutex_destroy(&m_mutex);
}

/**
 * @brief Push event to queue.
 *
 * @param[in] p_event_info: Pointer to event information structure.
 */
void omx_swvdec_queue::push(OMX_SWVDEC_EVENT_INFO *p_event_info)
{
    pthread_mutex_lock(&m_mutex);

    m_queue.push(*p_event_info);

    pthread_mutex_unlock(&m_mutex);
}

/**
 * @brief Pop event from queue.
 *
 * @param[in,out] p_event_info: Pointer to event information structure.
 *
 * @retval  true if pop successful
 * @retval false if pop unsuccessful
 */
bool omx_swvdec_queue::pop(OMX_SWVDEC_EVENT_INFO *p_event_info)
{
    bool retval = true;

    pthread_mutex_lock(&m_mutex);

    if (m_queue.empty())
    {
        retval = false;
    }
    else
    {
        *p_event_info = m_queue.front();

        m_queue.pop();
    }

    pthread_mutex_unlock(&m_mutex);

    return retval;
}

/**
 * @brief OMX SwVdec diagnostics class constructor.
 */
omx_swvdec_diag::omx_swvdec_diag():
    m_dump_ip(0),
    m_dump_op(0),
    m_filename_ip(NULL),
    m_filename_op(NULL),
    m_file_ip(NULL),
    m_file_op(NULL)
{
    time_t time_raw;

    struct tm *time_info;

    char time_string[16];

    char filename_ip[PROPERTY_VALUE_MAX];
    char filename_op[PROPERTY_VALUE_MAX];

    char property_value[PROPERTY_VALUE_MAX] = {0};

    time_raw = time(NULL);

    time_info = localtime(&time_raw);

    if (time_info != NULL)
    {
        // time string: "YYYYmmddTHHMMSS"
        strftime(time_string, sizeof(time_string), "%Y%m%dT%H%M%S", time_info);
    }
    else
    {
        // time string: "19700101T000000"
        snprintf(time_string, sizeof(time_string), "19700101T000000");
    }

    // default ip filename: "/data/misc/media/omx_swvdec_YYYYmmddTHHMMSS_ip.bin"
    snprintf(filename_ip,
             sizeof(filename_ip),
             "%s/omx_swvdec_%s_ip.bin",
             DIAG_FILE_PATH,
             time_string);

    // default op filename: "/data/misc/media/omx_swvdec_YYYYmmddTHHMMSS_op.yuv"
    snprintf(filename_op,
             sizeof(filename_op),
             "%s/omx_swvdec_%s_op.yuv",
             DIAG_FILE_PATH,
             time_string);

    if (property_get("vendor.omx_swvdec.dump.ip", property_value, NULL))
    {
        m_dump_ip = atoi(property_value);

        OMX_SWVDEC_LOG_HIGH("vendor.omx_swvdec.dump.ip: %d", m_dump_ip);
    }

    if (property_get("vendor.omx_swvdec.dump.op", property_value, NULL))
    {
        m_dump_op = atoi(property_value);

        OMX_SWVDEC_LOG_HIGH("vendor.omx_swvdec.dump.op: %d", m_dump_op);
    }

    if (m_dump_ip && property_get("vendor.omx_swvdec.filename.ip",
                                  property_value,
                                  filename_ip) && (strlen(property_value) > 0 ) )
    {
        size_t m_filename_ip_size = (strlen(property_value) + 1)*sizeof(char);
        m_filename_ip =
            (char *) malloc(m_filename_ip_size);
        if (m_filename_ip == NULL)
        {
            OMX_SWVDEC_LOG_ERROR("failed to allocate %zu bytes for "
                                 "input filename string",
                                 m_filename_ip_size);
        }
        else
        {
            strlcpy(m_filename_ip, property_value,m_filename_ip_size);
            OMX_SWVDEC_LOG_HIGH("vendor.omx_swvdec.filename.ip: %s", m_filename_ip);
            if ((m_file_ip = fopen(m_filename_ip, "wb")) == NULL)
            {
                OMX_SWVDEC_LOG_ERROR("cannot open input file '%s' logging erro is : %d",
                                     m_filename_ip,errno);
            }
        }
    }

    if (m_dump_op && property_get("vendor.omx_swvdec.filename.op",
                                  property_value,
                                  filename_op) && (strlen(property_value) > 0 ))
    {
        size_t m_filename_op_size = (strlen(property_value) + 1)*sizeof(char);
        m_filename_op =
            (char *) malloc(m_filename_op_size);
        if (m_filename_op == NULL)
        {
            OMX_SWVDEC_LOG_ERROR("failed to allocate %zu bytes for "
                                 "output filename string",
                                 m_filename_op_size);
        }
        else
        {
            strlcpy(m_filename_op, property_value,m_filename_op_size);
            OMX_SWVDEC_LOG_HIGH("vendor.omx_swvdec.filename.op: %s", m_filename_op);
            if ((m_file_op = fopen(m_filename_op, "wb")) == NULL)
            {
                OMX_SWVDEC_LOG_ERROR("cannot open output file '%s' logging error : %d",
                                     m_filename_op,errno);
            }
        }
    }
}

/**
 * @brief OMX SwVdec diagnostics class destructor.
 */
omx_swvdec_diag::~omx_swvdec_diag()
{
    if (m_file_op)
    {
        fclose(m_file_op);
        m_file_op = NULL;
    }

    if (m_file_ip)
    {
        fclose(m_file_ip);
        m_file_ip = NULL;
    }

    if (m_filename_op)
    {
        free(m_filename_op);
        m_filename_op = NULL;
    }

    if (m_filename_ip)
    {
        free(m_filename_ip);
        m_filename_ip = NULL;
    }
}

/**
 * @brief Dump input bitstream to file.
 *
 * @param[in] p_buffer:      Pointer to input bitstream buffer.
 * @param[in] filled_length: Bitstream buffer's filled length.
 */
void omx_swvdec_diag::dump_ip(unsigned char *p_buffer,
                              unsigned int   filled_length)
{
    if (m_dump_ip && (m_file_ip != NULL))
    {
        fwrite(p_buffer, sizeof(unsigned char), filled_length, m_file_ip);
    }
}

/**
 * @brief Dump output YUV to file.
 *
 * @param[in] p_buffer:  Pointer to output YUV buffer.
 * @param[in] width:     Frame width.
 * @param[in] height:    Frame height.
 * @param[in] stride:    Frame stride.
 * @param[in] scanlines: Frame scanlines.
 */
void omx_swvdec_diag::dump_op(unsigned char *p_buffer,
                              unsigned int   width,
                              unsigned int   height,
                              unsigned int   stride,
                              unsigned int   scanlines)
{
    if (m_dump_op && (m_file_op != NULL))
    {
        unsigned char *p_buffer_y;
        unsigned char *p_buffer_uv;

        unsigned int ii;

        p_buffer_y  = p_buffer;
        p_buffer_uv = p_buffer + (stride * scanlines);

        for (ii = 0; ii < height; ii++)
        {
            fwrite(p_buffer_y, sizeof(unsigned char), width, m_file_op);

            p_buffer_y += stride;
        }

        for (ii = 0; ii < (height / 2); ii++)
        {
            fwrite(p_buffer_uv, sizeof(unsigned char), width, m_file_op);

            p_buffer_uv += stride;
        }
    }
}
