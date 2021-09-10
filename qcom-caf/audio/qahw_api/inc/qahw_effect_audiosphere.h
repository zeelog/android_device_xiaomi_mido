/* Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.
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

#ifndef QAHW_EFFECT_AUDIOSPHERE_H_
#define QAHW_EFFECT_AUDIOSPHERE_H_

#include <qahw_effect_api.h>

#ifdef __cplusplus
extern "C" {
#endif

#define QAHW_EFFECT_AUDIOSPHERE_LIBRARY "libasphere.so"

static const qahw_effect_uuid_t SL_IID_AUDIOSPHERE_ = { 0x2f03ade8, 0xd92b, 0x4172, 0x9eea,
        { 0x52, 0x0c, 0xde, 0xfa, 0x3c, 0x1d } };
static const qahw_effect_uuid_t * const SL_IID_AUDIOSPHERE = &SL_IID_AUDIOSPHERE_;

static const qahw_effect_uuid_t SL_IID_AUDIOSPHERE_UUID_ = { 0x184e62ab, 0x2d19, 0x4364, 0x9d1b,
        { 0xc0, 0xa4, 0x07, 0x33, 0x86, 0x6c } };
static const qahw_effect_uuid_t * const SL_IID_AUDIOSPHERE_UUID = &SL_IID_AUDIOSPHERE_UUID_;

/* enumerated parameter settings for BassBoost effect */
typedef enum
{
    ASPHERE_PARAM_ENABLE,
    ASPHERE_PARAM_STRENGTH,
    ASPHERE_PARAM_STATUS,
} qahw_asphere_params;

#ifdef __cplusplus
}  // extern "C"
#endif


#endif /*QAHW_EFFECT_AUDIOSPHER_H_*/
