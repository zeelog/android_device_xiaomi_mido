/*
 * Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
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
#include <utils/Log.h>

#include "EGLImageWrapper.h"
#include "Tonemapper.h"
#include "engine.h"
#include "forward_tonemap.inl"
#include "fullscreen_vertex_shader.inl"
#include "rgba_inverse_tonemap.inl"

//-----------------------------------------------------------------------------
Tonemapper::Tonemapper()
//-----------------------------------------------------------------------------
{
  tonemapTexture = 0;
  lutXformTexture = 0;
  programID = 0;
  eglImageWrapper = new EGLImageWrapper();

  lutXformScaleOffset[0] = 1.0f;
  lutXformScaleOffset[1] = 0.0f;

  tonemapScaleOffset[0] = 1.0f;
  tonemapScaleOffset[1] = 0.0f;
}

//-----------------------------------------------------------------------------
Tonemapper::~Tonemapper()
//-----------------------------------------------------------------------------
{
  engine_bind(engineContext);
  engine_deleteInputBuffer(tonemapTexture);
  engine_deleteInputBuffer(lutXformTexture);
  engine_deleteProgram(programID);

  // clear EGLImage mappings
  if (eglImageWrapper != 0) {
    delete eglImageWrapper;
    eglImageWrapper = 0;
  }

  engine_shutdown(engineContext);
}

//-----------------------------------------------------------------------------
Tonemapper *Tonemapper::build(int type, void *colorMap, int colorMapSize, void *lutXform,
                              int lutXformSize, bool isSecure)
//-----------------------------------------------------------------------------
{
  if (colorMapSize <= 0) {
      ALOGE("Invalid Color Map size = %d", colorMapSize);
      return NULL;
  }

  // build new tonemapper
  Tonemapper *tonemapper = new Tonemapper();

  tonemapper->engineContext = engine_initialize(isSecure);

  engine_bind(tonemapper->engineContext);

  // load the 3d lut
  tonemapper->tonemapTexture = engine_load3DTexture(colorMap, colorMapSize, 0);
  tonemapper->tonemapScaleOffset[0] = ((float)(colorMapSize-1))/((float)(colorMapSize));
  tonemapper->tonemapScaleOffset[1] = 1.0f/(2.0f*colorMapSize);

  // load the non-uniform xform
  tonemapper->lutXformTexture = engine_load1DTexture(lutXform, lutXformSize, 0);
  bool bUseXform = (tonemapper->lutXformTexture != 0) && (lutXformSize != 0);
  if( bUseXform )
  {
      tonemapper->lutXformScaleOffset[0] = ((float)(lutXformSize-1))/((float)(lutXformSize));
      tonemapper->lutXformScaleOffset[1] = 1.0f/(2.0f*lutXformSize);
  }

  // create the program
  const char *fragmentShaders[3];
  int fragmentShaderCount = 0;
  const char *version = "#version 300 es\n";
  const char *define = "#define USE_NONUNIFORM_SAMPLING\n";

  fragmentShaders[fragmentShaderCount++] = version;

  // non-uniform sampling
  if (bUseXform) {
    fragmentShaders[fragmentShaderCount++] = define;
  }

  if (type == TONEMAP_INVERSE) {  // inverse tonemapping
    fragmentShaders[fragmentShaderCount++] = rgba_inverse_tonemap_shader;
  } else {  // forward tonemapping
    fragmentShaders[fragmentShaderCount++] = forward_tonemap_shader;
  }

  tonemapper->programID =
      engine_loadProgram(1, &fullscreen_vertex_shader, fragmentShaderCount, fragmentShaders);

  return tonemapper;
}

//-----------------------------------------------------------------------------
int Tonemapper::blit(const void *dst, const void *src, int srcFenceFd)
//-----------------------------------------------------------------------------
{
  // make current
  engine_bind(engineContext);

  // create eglimages if required
  EGLImageBuffer *dst_buffer = eglImageWrapper->wrap(dst);
  EGLImageBuffer *src_buffer = eglImageWrapper->wrap(src);

  // bind the program
  engine_setProgram(programID);

  engine_setData2f(3, tonemapScaleOffset);
  bool bUseXform = (lutXformTexture != 0);
  if( bUseXform )
  {
    engine_setData2f(4, lutXformScaleOffset);
  }

  // set destination
  if (dst_buffer) {
    engine_setDestination(dst_buffer->getFramebuffer(), 0, 0, dst_buffer->getWidth(),
                          dst_buffer->getHeight());
  }
  // set source
  if (src_buffer) {
    engine_setExternalInputBuffer(0, src_buffer->getTexture());
  }
  // set 3d lut
  engine_set3DInputBuffer(1, tonemapTexture);
  // set non-uniform xform
  engine_set2DInputBuffer(2, lutXformTexture);

  // perform
  int fenceFD = engine_blit(srcFenceFd);

  return fenceFD;
}
