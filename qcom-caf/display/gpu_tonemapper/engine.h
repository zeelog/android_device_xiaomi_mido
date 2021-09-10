/*
 * Copyright (c) 2016-2017, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
 * Copyright 2015 The Android Open Source Project
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

#ifndef __TONEMAPPER_ENGINE_H__
#define __TONEMAPPER_ENGINE_H__

void* engine_initialize(bool isSecure);
void engine_bind(void*);
void engine_shutdown(void*);

unsigned int engine_loadProgram(int, const char **, int, const char **);
void engine_setProgram(int);
void engine_deleteProgram(unsigned int);

unsigned int engine_load3DTexture(void *data, int sz, int format);
unsigned int engine_load1DTexture(void *xform, int xformSize, int format);
void engine_deleteInputBuffer(unsigned int);

void engine_set2DInputBuffer(int binding, unsigned int textureID);
void engine_set3DInputBuffer(int binding, unsigned int textureID);
void engine_setExternalInputBuffer(int binding, unsigned int textureID);
void engine_setDestination(int id, int x, int y, int w, int h);
void engine_setData2f(int loc, float* data);

int engine_blit(int);

#endif  //__TONEMAPPER_ENGINE_H__
