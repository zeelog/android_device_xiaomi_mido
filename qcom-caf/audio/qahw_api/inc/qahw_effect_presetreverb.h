/*
 * Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef QAHW_EFFECT_PRESETREVERB_H_
#define QAHW_EFFECT_PRESETREVERB_H_

#include <qahw_effect_api.h>

#ifdef __cplusplus
extern "C" {
#endif

#define QAHW_EFFECT_PRESET_REVERB_LIBRARY "libqcompostprocbundle.so"

static const qahw_effect_uuid_t SL_IID_PRESETREVERB_ = { 0x47382d60, 0xddd8, 0x11db, 0xbf3a,
        { 0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b } };
static const qahw_effect_uuid_t * const SL_IID_PRESETREVERB = &SL_IID_PRESETREVERB_;

static const qahw_effect_uuid_t SL_IID_INS_PRESETREVERB_UUID_ = { 0xaa2bebf6, 0x47cf, 0x4613, 0x9bca,
        { 0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b } };
static const qahw_effect_uuid_t * const SL_IID_INS_PRESETREVERB_UUID = &SL_IID_INS_PRESETREVERB_UUID_;

static const qahw_effect_uuid_t SL_IID_AUX_PRESETREVERB_UUID_ = { 0x6987be09, 0xb142, 0x4b41, 0x9056,
        { 0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b } };
static const qahw_effect_uuid_t * const SL_IID_AUX_PRESETREVERB_UUID = &SL_IID_AUX_PRESETREVERB_UUID_;

/* enumerated parameter settings for preset reverb effect */
typedef enum
{
    REVERB_PARAM_PRESET
} qahw_preset_reverb_params;


typedef enum
{
    REVERB_PRESET_NONE,
    REVERB_PRESET_SMALLROOM,
    REVERB_PRESET_MEDIUMROOM,
    REVERB_PRESET_LARGEROOM,
    REVERB_PRESET_MEDIUMHALL,
    REVERB_PRESET_LARGEHALL,
    REVERB_PRESET_PLATE,
    REVERB_PRESET_LAST = REVERB_PRESET_PLATE
} qahw_reverb_presets;

#ifdef __cplusplus
}  // extern "C"
#endif


#endif /*QAHW_EFFECT_PRESETREVERB_H_*/
