/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
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
 * */

#include "aacEncode.h"
#include "aacenc_lib.h"
#include <utils/Log.h>
#include <string.h>

struct aacInfo
{
    AACENC_BufDesc inBuff;
    AACENC_BufDesc outBuff;
    AACENC_InArgs  inArg;
    AACENC_OutArgs outArg;
};

aacEncode::aacEncode() {
    p_aacHandle = NULL;
    p_aacInfo = NULL;
    memset(&s_aacConfig, 0, sizeof(s_aacConfig));
}

aacEncode::~aacEncode() {
    if(!p_aacHandle) {
        return;
    }

    if(aacEncClose((HANDLE_AACENCODER*)(&p_aacHandle)) != AACENC_OK) {
        ALOGE("aacEncClose Failed");
        return;
    }
}

bool aacEncode::aacConfigure(aacConfigType * p_aacConfig) {
    if(!p_aacConfig) {
        return false;
    }

    memcpy(&s_aacConfig, p_aacConfig, sizeof(s_aacConfig));

    /* Configure AAC encoder here */

    AACENC_ERROR err = AACENC_OK;

    p_aacInfo = (void*)new(aacInfo);
    if(!p_aacInfo) {
        ALOGE("Failed to allocate aacInfo");
        return false;
    }

    /* Open AAC encoder */
    err = aacEncOpen((HANDLE_AACENCODER*)(&p_aacHandle),
                      0x01 /* AAC */,
                      s_aacConfig.n_channels);

    if(err != AACENC_OK) {
        ALOGE("Failed top open AAC encoder");
        return false;
    }

    /* Set Bitrate and SampleRate */
    err = aacEncoder_SetParam((HANDLE_AACENCODER)p_aacHandle,
                               AACENC_BITRATE,
                               s_aacConfig.n_bitrate);

    if(err != AACENC_OK) {
        ALOGE("Failed to set bitrate param to AAC encoder");
        return false;
    }

    err = aacEncoder_SetParam((HANDLE_AACENCODER)p_aacHandle,
                                   AACENC_SAMPLERATE,
                                   s_aacConfig.n_sampleRate);

    if(err != AACENC_OK) {
        ALOGE("Failed to set samplerate param to AAC encoder");
        return false;
    }

    /* Fix Channel mode and order */
    /* TODO */

    /* Prefill encode structures */
    /* TODO */

    return true;
}

bool aacEncode::aacEncodeFrame(unsigned char * p_inBuffer,
                              unsigned int n_inSize,
                              unsigned char * p_outBuffer,
                              unsigned int n_outSize,
                              unsigned int * p_length) {
    (void)n_inSize;
    (void)n_outSize;
    (void)p_length;
    if(!p_inBuffer || !p_outBuffer) {
        ALOGE("No buffers provided for AAC encoder");
        return false;
    }

    aacInfo *tempAacInfo = (aacInfo*)p_aacInfo;
    tempAacInfo->inBuff.bufs = (void**) (&p_inBuffer);
    tempAacInfo->outBuff.bufs = (void**) (&p_outBuffer);

    AACENC_ERROR err = AACENC_OK;

    if(p_aacHandle) {
        err = aacEncEncode((HANDLE_AACENCODER)p_aacHandle,
                           &tempAacInfo->inBuff,
                           &tempAacInfo->outBuff,
                           &tempAacInfo->inArg,
                           &tempAacInfo->outArg);

        if(err != AACENC_OK) {
            ALOGE("Failed to encode buffer");
            return false;
        }
    } else {
        ALOGE("No encoder available");
        return false;
    }

    return true;
}
