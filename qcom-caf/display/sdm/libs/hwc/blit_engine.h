/*
* Copyright (c) 2015, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*  * Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
*  * Redistributions in binary form must reproduce the above
*    copyright notice, this list of conditions and the following
*    disclaimer in the documentation and/or other materials provided
*    with the distribution.
*  * Neither the name of The Linux Foundation nor the names of its
*    contributors may be used to endorse or promote products derived
*    from this software without specific prior written permission.
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
*/

/*! @file blit_engine.h
  @brief Interface file for Blit based compositior.

  @details The client can use this interface to get the blit composition done

*/

#ifndef __BLIT_ENGINE_H__
#define __BLIT_ENGINE_H__

namespace sdm {

/*! @brief Blit Engine implemented by the client

  @details This class declares prototype for BlitEngine Interface which must be
  implemented by the client. HWC will use this interface to use a Blit engine to get the
  composition done.

*/
class BlitEngine {
 public:
  BlitEngine() { }
  virtual ~BlitEngine() { }

  virtual int Init() = 0;
  virtual void DeInit() = 0;
  virtual int Prepare(LayerStack *layer_stack) = 0;
  virtual int PreCommit(hwc_display_contents_1_t *content_list, LayerStack *layer_stack) = 0;
  virtual int Commit(hwc_display_contents_1_t *content_list, LayerStack *layer_stack) = 0;
  virtual void PostCommit(LayerStack *layer_stack) = 0;
  virtual bool BlitActive() = 0;
  virtual void SetFrameDumpConfig(uint32_t count) = 0;
};

}  // namespace sdm

#endif  // __BLIT_ENGINE_H__
