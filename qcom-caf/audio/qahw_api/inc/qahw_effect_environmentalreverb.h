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

#ifndef QAHW_EFFECT_ENVIRONMENTALREVERB_H_
#define QAHW_EFFECT_ENVIRONMENTALREVERB_H_

#include <qahw_effect_api.h>

#ifdef __cplusplus
extern "C" {
#endif

#define QAHW_EFFECT_ENV_REVERB_LIBRARY "libqcompostprocbundle.so"

static const qahw_effect_uuid_t SL_IID_ENVIRONMENTALREVERB_ = { 0xc2e5d5f0, 0x94bd, 0x4763, 0x9cac,
        { 0x4e, 0x23, 0x4d, 0x6, 0x83, 0x9e } };
static const qahw_effect_uuid_t * const SL_IID_ENVIRONMENTALREVERB = &SL_IID_ENVIRONMENTALREVERB_;

static const qahw_effect_uuid_t SL_IID_INS_ENVIRONMENTALREVERB_UUID_ = { 0xeb64ea04, 0x973b, 0x43d2, 0x8f5e,
        { 0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b } };
static const qahw_effect_uuid_t * const SL_IID_INS_ENVIRONMENTALREVERB_UUID = &SL_IID_INS_ENVIRONMENTALREVERB_UUID_;

static const qahw_effect_uuid_t SL_IID_AUX_ENVIRONMENTALREVERB_UUID_ = { 0x79a18026, 0x18fd, 0x4185, 0x8233,
        { 0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b } };
static const qahw_effect_uuid_t * const SL_IID_AUX_ENVIRONMENTALREVERB_UUID = &SL_IID_AUX_ENVIRONMENTALREVERB_UUID_;

/* enumerated parameter settings for environmental reverb effect */
typedef enum
{
    // Parameters below are as defined in OpenSL ES specification for environmental reverb interface
    REVERB_PARAM_ROOM_LEVEL,            // in millibels,    range -6000 to 0
    REVERB_PARAM_ROOM_HF_LEVEL,         // in millibels,    range -4000 to 0
    REVERB_PARAM_DECAY_TIME,            // in milliseconds, range 100 to 20000
    REVERB_PARAM_DECAY_HF_RATIO,        // in permilles,    range 100 to 1000
    REVERB_PARAM_REFLECTIONS_LEVEL,     // in millibels,    range -6000 to 0
    REVERB_PARAM_REFLECTIONS_DELAY,     // in milliseconds, range 0 to 65
    REVERB_PARAM_REVERB_LEVEL,          // in millibels,    range -6000 to 0
    REVERB_PARAM_REVERB_DELAY,          // in milliseconds, range 0 to 65
    REVERB_PARAM_DIFFUSION,             // in permilles,    range 0 to 1000
    REVERB_PARAM_DENSITY,               // in permilles,    range 0 to 1000
    REVERB_PARAM_PROPERTIES,
    REVERB_PARAM_BYPASS,
    REVERB_PARAM_LATENCY = 0x80000000   // Internal paramter specific to qahw.
                                        // Used to get latency introduced by reverb effect.
} qahw_env_reverb_params;

//qahw_reverb_settings is equal to SLEnvironmentalReverbSettings defined in OpenSL ES specification.
typedef struct s_reverb_settings {
    int16_t     roomLevel;
    int16_t     roomHFLevel;
    uint32_t    decayTime;
    int16_t     decayHFRatio;
    int16_t     reflectionsLevel;
    uint32_t    reflectionsDelay;
    int16_t     reverbLevel;
    uint32_t    reverbDelay;
    int16_t     diffusion;
    int16_t     density;
} __attribute__((packed)) qahw_reverb_settings;


#ifdef __cplusplus
}  // extern "C"
#endif


#endif /*QAHW_EFFECT_ENVIRONMENTALREVERB_H_*/
