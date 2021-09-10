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

#include "aacDecode.h"
#include "aacdecoder_lib.h"
#include <utils/Log.h>
#include <string.h>

aacDecode::aacDecode() {
    p_aacHandle = NULL;
    p_aacInfo = NULL;
    memset(&s_aacConfig, 0, sizeof(s_aacConfig));
}

aacDecode::~aacDecode() {
    if(!p_aacHandle) {
        return;
    }

    aacDecoder_Close((HANDLE_AACDECODER)p_aacHandle);
    p_aacHandle = NULL;
}

bool aacDecode::aacConfigure(aacConfigType * p_aacConfig) {
    if(!p_aacConfig) {
        return false;
    }

    memcpy(&s_aacConfig, p_aacConfig, sizeof(s_aacConfig));

    p_aacHandle = aacDecoder_Open(TT_MP4_ADTS, 1);

    if(!p_aacHandle) {
        ALOGE("Failed to open AAC decoder");
        return false;
    }

    p_aacInfo = (void*)aacDecoder_GetStreamInfo((HANDLE_AACDECODER)p_aacHandle);

    if(!p_aacInfo) {
        ALOGE("Failed to get stream info");
        return false;
    }

    /* Configure AAC decoder */

    aacDecoder_SetParam((HANDLE_AACDECODER)p_aacHandle,
                        AAC_DRC_REFERENCE_LEVEL,
                        64);

    aacDecoder_SetParam((HANDLE_AACDECODER)p_aacHandle,
                        AAC_DRC_ATTENUATION_FACTOR,
                        127);

    aacDecoder_SetParam((HANDLE_AACDECODER)p_aacHandle,
                        AAC_PCM_MAX_OUTPUT_CHANNELS,
                        s_aacConfig.n_channels > 6 ? -1 : s_aacConfig.n_channels);
    return true;
}

bool aacDecode::aacDecodeFrame(unsigned char* p_buffer, unsigned int n_size) {
    if(!p_buffer || n_size < 7) {
        ALOGE("No/Incorrect buffer provided for AAC decoder");
        return false;
    }

    if(!p_aacHandle) {
        ALOGE("Decoder handle not available");
        return false;
    }

    AAC_DECODER_ERROR err = AAC_DEC_UNKNOWN;

    UINT nSize[] = {(UINT)n_size};
    UINT nOcc[] = {(UINT)n_size};
    UCHAR* pBuf[] = {p_buffer};

    err = aacDecoder_Fill((HANDLE_AACDECODER)p_aacHandle, pBuf, nSize, nOcc);

    if(err != AAC_DEC_OK || nOcc[0] != 0) {
        ALOGE("Error in aacDecoder_Fill");
        return false;
    }

    /* Fix the alloc length being passed to AAC Decoder since it expects INT_PCM
       as input buffer but we have it as UINT8. INT_PCM is of type SHORT */

    int factor = sizeof(INT_PCM)/sizeof(unsigned char);
    int allocLen = (factor == 0) ? n_size : n_size/factor;

    err = aacDecoder_DecodeFrame((HANDLE_AACDECODER)p_aacHandle,
                                  (INT_PCM*)p_buffer, allocLen, 0);

    if(err != AAC_DEC_OK) {
        ALOGE("Failed to decode frame");
        return false;
    }
    return true;
}
