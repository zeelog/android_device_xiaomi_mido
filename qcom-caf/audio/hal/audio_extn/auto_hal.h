/*
 * Copyright (c) 2019 The Linux Foundation. All rights reserved.
 * Not a contribution.
 *
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <audio_hw.h>
#include "platform.h"

#define MAX_SOURCE_PORTS_PER_PATCH 1
#define MAX_SINK_PORTS_PER_PATCH 1

/* Volume min/max defined by audio policy configuration in millibel.
 * Support a range of -60dB to 6dB.
 */
#define MIN_VOLUME_VALUE_MB -6000
#define MAX_VOLUME_VALUE_MB 600
#define STEP_VALUE_MB 100

#define MIN_VOLUME_GAIN 0.0f
#define MAX_VOLUME_GAIN 1.0f

typedef struct auto_hal_module {
    struct audio_device *adev;
    card_status_t card_status;
} auto_hal_module_t;

struct pcm_config pcm_config_media = {
    .channels = 2,
    .rate = DEFAULT_OUTPUT_SAMPLING_RATE,
    .period_size = DEEP_BUFFER_OUTPUT_PERIOD_SIZE,
    .period_count = DEEP_BUFFER_OUTPUT_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = DEEP_BUFFER_OUTPUT_PERIOD_SIZE / 4,
    .stop_threshold = INT_MAX,
    .avail_min = DEEP_BUFFER_OUTPUT_PERIOD_SIZE / 4,
};

struct pcm_config pcm_config_system = {
    .channels = 2,
    .rate = DEFAULT_OUTPUT_SAMPLING_RATE,
    .period_size = LOW_LATENCY_OUTPUT_PERIOD_SIZE,
    .period_count = LOW_LATENCY_OUTPUT_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = LOW_LATENCY_OUTPUT_PERIOD_SIZE / 4,
    .stop_threshold = INT_MAX,
    .avail_min = LOW_LATENCY_OUTPUT_PERIOD_SIZE / 4,
};

static const audio_usecase_t bus_device_usecases[] = {
    USECASE_AUDIO_PLAYBACK_MEDIA,
    USECASE_AUDIO_PLAYBACK_SYS_NOTIFICATION,
    USECASE_AUDIO_PLAYBACK_NAV_GUIDANCE,
    USECASE_AUDIO_PLAYBACK_PHONE,
    USECASE_AUDIO_PLAYBACK_FRONT_PASSENGER,
    USECASE_AUDIO_PLAYBACK_REAR_SEAT,
};
