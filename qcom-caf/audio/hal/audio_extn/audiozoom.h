/*
 * Copyright (C) 2019 The Android Open Source Project
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

#ifndef AUDIOZOOM_H_
#define AUDIOZOOM_H_

int audiozoom_init(audiozoom_init_config_t init_config);
int audiozoom_set_microphone_direction(struct stream_in *stream,
                                           audio_microphone_direction_t dir);
int audiozoom_set_microphone_field_dimension(struct stream_in *stream, float zoom);

#endif /* AUDIOZOOM_H_ */
