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

#include "glengine.h"
#include <log/log.h>
#include "engine.h"

void checkGlError(const char *, int);
void checkEglError(const char *, int);

class EngineContext {
    public:
    EGLDisplay eglDisplay;
    EGLContext eglContext;
    EGLSurface eglSurface;
    EngineContext()
    {
        eglDisplay = EGL_NO_DISPLAY;
        eglContext = EGL_NO_CONTEXT;
        eglSurface = EGL_NO_SURFACE;
    }
};

//-----------------------------------------------------------------------------
// Make Current
void engine_bind(void* context)
//-----------------------------------------------------------------------------
{
  EngineContext* engineContext = (EngineContext*)(context);
  EGL(eglMakeCurrent(engineContext->eglDisplay, engineContext->eglSurface, engineContext->eglSurface, engineContext->eglContext));
}

//-----------------------------------------------------------------------------
// initialize GL
//
void* engine_initialize(bool isSecure)
//-----------------------------------------------------------------------------
{
  EngineContext* engineContext = new EngineContext();

  // display
  engineContext->eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  EGL(eglBindAPI(EGL_OPENGL_ES_API));

  // initialize
  EGL(eglInitialize(engineContext->eglDisplay, 0, 0));

  // config
  EGLConfig eglConfig;
  EGLint eglConfigAttribList[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                                  EGL_RED_SIZE,     8,
                                  EGL_GREEN_SIZE,   8,
                                  EGL_BLUE_SIZE,    8,
                                  EGL_ALPHA_SIZE,   8,
                                  EGL_NONE};
  int numConfig = 0;
  EGL(eglChooseConfig(engineContext->eglDisplay, eglConfigAttribList, &eglConfig, 1, &numConfig));

  // context
  EGLint eglContextAttribList[] = {EGL_CONTEXT_CLIENT_VERSION, 3,
                                   isSecure ? EGL_PROTECTED_CONTENT_EXT : EGL_NONE,
                                   isSecure ? EGL_TRUE : EGL_NONE,
                                   EGL_NONE};
  engineContext->eglContext = eglCreateContext(engineContext->eglDisplay, eglConfig, NULL, eglContextAttribList);

  // surface
  EGLint eglSurfaceAttribList[] = {EGL_WIDTH, 1,
                                   EGL_HEIGHT, 1,
                                   isSecure ? EGL_PROTECTED_CONTENT_EXT : EGL_NONE,
                                   isSecure ? EGL_TRUE : EGL_NONE,
                                   EGL_NONE};
  engineContext->eglSurface = eglCreatePbufferSurface(engineContext->eglDisplay, eglConfig, eglSurfaceAttribList);

  eglMakeCurrent(engineContext->eglDisplay, engineContext->eglSurface, engineContext->eglSurface, engineContext->eglContext);

  ALOGI("In %s context = %p", __FUNCTION__, (void *)(engineContext->eglContext));

  return (void*)(engineContext);
}

//-----------------------------------------------------------------------------
// Shutdown.
void engine_shutdown(void* context)
//-----------------------------------------------------------------------------
{
  EngineContext* engineContext = (EngineContext*)context;
  EGL(eglMakeCurrent(engineContext->eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
  EGL(eglDestroySurface(engineContext->eglDisplay, engineContext->eglSurface));
  EGL(eglDestroyContext(engineContext->eglDisplay, engineContext->eglContext));
  EGL(eglTerminate(engineContext->eglDisplay));
  engineContext->eglDisplay = EGL_NO_DISPLAY;
  engineContext->eglContext = EGL_NO_CONTEXT;
  engineContext->eglSurface = EGL_NO_SURFACE;
}

//-----------------------------------------------------------------------------
void engine_deleteInputBuffer(unsigned int id)
//-----------------------------------------------------------------------------
{
  if (id != 0) {
    GL(glDeleteTextures(1, &id));
  }
}

//-----------------------------------------------------------------------------
void engine_deleteProgram(unsigned int id)
//-----------------------------------------------------------------------------
{
  if (id != 0) {
    GL(glDeleteProgram(id));
  }
}

//-----------------------------------------------------------------------------
void engine_setData2f(int location, float* data)
//-----------------------------------------------------------------------------
{
    GL(glUniform2f(location, data[0], data[1]));
}

//-----------------------------------------------------------------------------
unsigned int engine_load3DTexture(void *colorMapData, int sz, int format)
//-----------------------------------------------------------------------------
{
  GLuint texture = 0;
  GL(glGenTextures(1, &texture));
  GL(glBindTexture(GL_TEXTURE_3D, texture));
  GL(glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
  GL(glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
  GL(glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE));
  GL(glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
  GL(glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));

  GL(glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB10_A2, sz, sz, sz, 0, GL_RGBA,
                  GL_UNSIGNED_INT_2_10_10_10_REV, colorMapData));

  return texture;
}
//-----------------------------------------------------------------------------
unsigned int engine_load1DTexture(void *data, int sz, int format)
//-----------------------------------------------------------------------------
{
  GLuint texture = 0;
  if ((data != 0) && (sz != 0)) {
    GL(glGenTextures(1, &texture));
    GL(glBindTexture(GL_TEXTURE_2D, texture));
    GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));

    GL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB10_A2, sz, 1, 0, GL_RGBA,
                    GL_UNSIGNED_INT_2_10_10_10_REV, data));
  }
  return texture;
}

//-----------------------------------------------------------------------------
void dumpShaderLog(int shader)
//-----------------------------------------------------------------------------
{
  int success = 0;
  GLchar infoLog[512];
  GL(glGetShaderiv(shader, GL_COMPILE_STATUS, &success));
  if (!success) {
    glGetShaderInfoLog(shader, 512, NULL, infoLog);
    ALOGI("Shader Failed to compile: %s\n", infoLog);
  }
}

//-----------------------------------------------------------------------------
GLuint engine_loadProgram(int vertexEntries, const char **vertex, int fragmentEntries,
                          const char **fragment)
//-----------------------------------------------------------------------------
{
  GLuint progId = glCreateProgram();

  int vertId = glCreateShader(GL_VERTEX_SHADER);
  int fragId = glCreateShader(GL_FRAGMENT_SHADER);

  GL(glShaderSource(vertId, vertexEntries, vertex, 0));
  GL(glCompileShader(vertId));
  dumpShaderLog(vertId);

  GL(glShaderSource(fragId, fragmentEntries, fragment, 0));
  GL(glCompileShader(fragId));
  dumpShaderLog(fragId);

  GL(glAttachShader(progId, vertId));
  GL(glAttachShader(progId, fragId));

  GL(glLinkProgram(progId));

  GL(glDetachShader(progId, vertId));
  GL(glDetachShader(progId, fragId));

  GL(glDeleteShader(vertId));
  GL(glDeleteShader(fragId));

  return progId;
}

//-----------------------------------------------------------------------------
void WaitOnNativeFence(int fd)
//-----------------------------------------------------------------------------
{
  if (fd != -1) {
    EGLint attribs[] = {EGL_SYNC_NATIVE_FENCE_FD_ANDROID, fd, EGL_NONE};

    EGLSyncKHR sync = eglCreateSyncKHR(eglGetCurrentDisplay(), EGL_SYNC_NATIVE_FENCE_ANDROID, attribs);

    if (sync == EGL_NO_SYNC_KHR) {
      ALOGE("%s - Failed to Create sync from source fd", __FUNCTION__);
    } else {
      // the gpu will wait for this sync - not this cpu thread.
      EGL(eglWaitSyncKHR(eglGetCurrentDisplay(), sync, 0));
      EGL(eglDestroySyncKHR(eglGetCurrentDisplay(), sync));
    }
  }
}

//-----------------------------------------------------------------------------
int CreateNativeFence()
//-----------------------------------------------------------------------------
{
  int fd = -1;

  EGLSyncKHR sync = eglCreateSyncKHR(eglGetCurrentDisplay(), EGL_SYNC_NATIVE_FENCE_ANDROID, NULL);
  GL(glFlush());
  if (sync == EGL_NO_SYNC_KHR) {
    ALOGE("%s - Failed to Create Native Fence sync", __FUNCTION__);
  } else {
    fd = eglDupNativeFenceFDANDROID(eglGetCurrentDisplay(), sync);
    if (fd == EGL_NO_NATIVE_FENCE_FD_ANDROID) {
      ALOGE("%s - Failed to dup sync", __FUNCTION__);
    }
    EGL(eglDestroySyncKHR(eglGetCurrentDisplay(), sync));
  }

  return fd;
}

//-----------------------------------------------------------------------------
void engine_setDestination(int id, int x, int y, int w, int h)
//-----------------------------------------------------------------------------
{
  GL(glBindFramebuffer(GL_FRAMEBUFFER, id));
  GL(glViewport(x, y, w, h));
}

//-----------------------------------------------------------------------------
void engine_setProgram(int id)
//-----------------------------------------------------------------------------
{
  GL(glUseProgram(id));
}

//-----------------------------------------------------------------------------
void engine_set2DInputBuffer(int binding, unsigned int id)
//-----------------------------------------------------------------------------
{
  GL(glActiveTexture(GL_TEXTURE0 + binding));
  GL(glBindTexture(GL_TEXTURE_2D, id));
}

//-----------------------------------------------------------------------------
void engine_set3DInputBuffer(int binding, unsigned int id)
//-----------------------------------------------------------------------------
{
  GL(glActiveTexture(GL_TEXTURE0 + binding));
  GL(glBindTexture(GL_TEXTURE_3D, id));
}

//-----------------------------------------------------------------------------
void engine_setExternalInputBuffer(int binding, unsigned int id)
//-----------------------------------------------------------------------------
{
  GL(glActiveTexture(GL_TEXTURE0 + binding));
  GL(glBindTexture(0x8D65, id));
}

//-----------------------------------------------------------------------------
int engine_blit(int srcFenceFd)
//-----------------------------------------------------------------------------
{
  int fd = -1;
  WaitOnNativeFence(srcFenceFd);
  float fullscreen_vertices[]{0.0f, 2.0f, 0.0f, 0.0f, 2.0f, 0.0f};
  GL(glEnableVertexAttribArray(0));
  GL(glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, fullscreen_vertices));
  GL(glDrawArrays(GL_TRIANGLES, 0, 3));
  fd = CreateNativeFence();
  GL(glFlush());
  return fd;
}

//-----------------------------------------------------------------------------
void checkGlError(const char *file, int line)
//-----------------------------------------------------------------------------
{
  for (GLint error = glGetError(); error; error = glGetError()) {
    const char *pError = "<unknown error>";
    switch (error) {
      case GL_NO_ERROR:
        pError = "GL_NO_ERROR";
        break;
      case GL_INVALID_ENUM:
        pError = "GL_INVALID_ENUM";
        break;
      case GL_INVALID_VALUE:
        pError = "GL_INVALID_VALUE";
        break;
      case GL_INVALID_OPERATION:
        pError = "GL_INVALID_OPERATION";
        break;
      case GL_OUT_OF_MEMORY:
        pError = "GL_OUT_OF_MEMORY";
        break;
      case GL_INVALID_FRAMEBUFFER_OPERATION:
        pError = "GL_INVALID_FRAMEBUFFER_OPERATION";
        break;
    }

    ALOGE("glError (%s) %s:%d\n", pError, file, line);
  }
}

//-----------------------------------------------------------------------------
void checkEglError(const char *file, int line)
//-----------------------------------------------------------------------------
{
  for (int i = 0; i < 5; i++) {
    const EGLint error = eglGetError();
    if (error == EGL_SUCCESS) {
      break;
    }

    const char *pError = "<unknown error>";
    switch (error) {
      case EGL_SUCCESS:
        pError = "EGL_SUCCESS";
        break;
      case EGL_NOT_INITIALIZED:
        pError = "EGL_NOT_INITIALIZED";
        break;
      case EGL_BAD_ACCESS:
        pError = "EGL_BAD_ACCESS";
        break;
      case EGL_BAD_ALLOC:
        pError = "EGL_BAD_ALLOC";
        break;
      case EGL_BAD_ATTRIBUTE:
        pError = "EGL_BAD_ATTRIBUTE";
        break;
      case EGL_BAD_CONTEXT:
        pError = "EGL_BAD_CONTEXT";
        break;
      case EGL_BAD_CONFIG:
        pError = "EGL_BAD_CONFIG";
        break;
      case EGL_BAD_CURRENT_SURFACE:
        pError = "EGL_BAD_CURRENT_SURFACE";
        break;
      case EGL_BAD_DISPLAY:
        pError = "EGL_BAD_DISPLAY";
        break;
      case EGL_BAD_SURFACE:
        pError = "EGL_BAD_SURFACE";
        break;
      case EGL_BAD_MATCH:
        pError = "EGL_BAD_MATCH";
        break;
      case EGL_BAD_PARAMETER:
        pError = "EGL_BAD_PARAMETER";
        break;
      case EGL_BAD_NATIVE_PIXMAP:
        pError = "EGL_BAD_NATIVE_PIXMAP";
        break;
      case EGL_BAD_NATIVE_WINDOW:
        pError = "EGL_BAD_NATIVE_WINDOW";
        break;
      case EGL_CONTEXT_LOST:
        pError = "EGL_CONTEXT_LOST";
        break;
    }
    ALOGE("eglError (%s) %s:%d\n", pError, file, line);
  }
}
