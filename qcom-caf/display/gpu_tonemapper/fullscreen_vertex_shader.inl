/*
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
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

const char* fullscreen_vertex_shader = "                                      "
"#version 300 es                                                            \n"
"precision highp float;                                                     \n"
"layout(location = 0) in vec2 iUV;                                          \n"
"out vec2 uv;                                                               \n"
"void main()                                                                \n"
"{                                                                          \n"
"    vec2 positions[3];                                                     \n"
"    positions[0] = vec2(-1.0f, 3.0f);                                      \n"
"    positions[1] = vec2(-1.0f, -1.0f);                                     \n"
"    positions[2] = vec2(3.0f, -1.0f);                                      \n"
"    vec2 uvs[3];                                                           \n"
"    uvs[0] = vec2(0.0f, -1.0f);                                            \n"
"    uvs[1] = vec2(0.0f, 1.0f);                                             \n"
"    uvs[2] = vec2(2.0f, 1.0f);                                             \n"
"    gl_Position = vec4(positions[gl_VertexID], -1.0f, 1.0f);               \n"
"    uv = uvs[gl_VertexID];                                                 \n"
"}                                                                          \n";
