/*
* Copyright (c) 2015-2017, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above
*     copyright notice, this list of conditions and the following
*     disclaimer in the documentation and/or other materials provided
*     with the distribution.
*   * Neither the name of The Linux Foundation nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
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

#ifndef __RECT_H__
#define __RECT_H__

#include <stdint.h>
#include <core/sdm_types.h>
#include <core/layer_stack.h>
#include <utils/debug.h>

namespace sdm {

  enum RectOrientation {
    kOrientationPortrait,
    kOrientationLandscape,
    kOrientationUnknown,
  };

  bool IsValid(const LayerRect &rect);
  bool IsCongruent(const LayerRect &rect1, const LayerRect &rect2);
  void LogI(DebugTag debug_tag, const char *prefix, const LayerRect &roi);
  void Log(DebugTag debug_tag, const char *prefix, const LayerRect &roi);
  void Normalize(const uint32_t &align_x, const uint32_t &align_y, LayerRect *rect);
  LayerRect Union(const LayerRect &rect1, const LayerRect &rect2);
  LayerRect Intersection(const LayerRect &rect1, const LayerRect &rect2);
  LayerRect Subtract(const LayerRect &rect1, const LayerRect &rect2);
  LayerRect Reposition(const LayerRect &rect1, const int &x_offset, const int &y_offset);
  void SplitLeftRight(const LayerRect &in_rect, uint32_t split_count, uint32_t align_x,
                      bool flip_horizontal, LayerRect *out_rects);
  void SplitTopBottom(const LayerRect &in_rect, uint32_t split_count, uint32_t align_y,
                      bool flip_horizontal, LayerRect *out_rects);
  void MapRect(const LayerRect &src_domain, const LayerRect &dst_domain, const LayerRect &in_rect,
               LayerRect *out_rect);
  void TransformHV(const LayerRect &src_domain, const LayerRect &in_rect,
                   const LayerTransform &transform, LayerRect *out_rect);
  RectOrientation GetOrientation(const LayerRect &in_rect);
}  // namespace sdm

#endif  // __RECT_H__

