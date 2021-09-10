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

#include <math.h>
#include <utils/rect.h>
#include <utils/constants.h>
#include <algorithm>

#define __CLASS__ "RectUtils"

namespace sdm {

bool IsValid(const LayerRect &rect) {
  return ((rect.bottom > rect.top) && (rect.right > rect.left));
}

bool IsCongruent(const LayerRect &rect1, const LayerRect &rect2) {
  return ((rect1.left == rect2.left) &&
          (rect1.top == rect2.top) &&
          (rect1.right == rect2.right) &&
          (rect1.bottom == rect2.bottom));
}

void LogI(DebugTag debug_tag, const char *prefix, const LayerRect &roi) {
  DLOGI_IF(debug_tag, "%s: left = %.0f, top = %.0f, right = %.0f, bottom = %.0f",
           prefix, roi.left, roi.top, roi.right, roi.bottom);
}

void Log(DebugTag debug_tag, const char *prefix, const LayerRect &roi) {
  DLOGV_IF(debug_tag, "%s: left = %.0f, top = %.0f, right = %.0f, bottom = %.0f",
           prefix, roi.left, roi.top, roi.right, roi.bottom);
}

void Normalize(const uint32_t &align_x, const uint32_t &align_y, LayerRect *rect) {
    rect->left = ROUND_UP_ALIGN_UP(rect->left, align_x);
    rect->right = ROUND_UP_ALIGN_DOWN(rect->right, align_x);
    rect->top = ROUND_UP_ALIGN_UP(rect->top, align_y);
    rect->bottom = ROUND_UP_ALIGN_DOWN(rect->bottom, align_y);
}

LayerRect Intersection(const LayerRect &rect1, const LayerRect &rect2) {
  LayerRect res;

  if (!IsValid(rect1) || !IsValid(rect2)) {
    return LayerRect();
  }

  res.left = std::max(rect1.left, rect2.left);
  res.top = std::max(rect1.top, rect2.top);
  res.right = std::min(rect1.right, rect2.right);
  res.bottom = std::min(rect1.bottom, rect2.bottom);

  if (!IsValid(res)) {
    return LayerRect();
  }

  return res;
}

LayerRect Reposition(const LayerRect &rect, const int &x_offset, const int &y_offset) {
  LayerRect res;

  if (!IsValid(rect)) {
    return LayerRect();
  }

  res.left = rect.left + FLOAT(x_offset);
  res.top = rect.top + FLOAT(y_offset);
  res.right = rect.right + FLOAT(x_offset);
  res.bottom = rect.bottom + FLOAT(y_offset);

  return res;
}

// Not a geometrical rect deduction. Deducts rect2 from rect1 only if it results a single rect
LayerRect Subtract(const LayerRect &rect1, const LayerRect &rect2) {
  LayerRect res;

  res = rect1;

  if ((rect1.left == rect2.left) && (rect1.right == rect2.right)) {
    if ((rect1.top == rect2.top) && (rect2.bottom <= rect1.bottom)) {
      res.top = rect2.bottom;
    } else if ((rect1.bottom == rect2.bottom) && (rect2.top >= rect1.top)) {
      res.bottom = rect2.top;
    }
  } else if ((rect1.top == rect2.top) && (rect1.bottom == rect2.bottom)) {
    if ((rect1.left == rect2.left) && (rect2.right <= rect1.right)) {
      res.left = rect2.right;
    } else if ((rect1.right == rect2.right) && (rect2.left >= rect1.left)) {
      res.right = rect2.left;
    }
  }

  return res;
}

LayerRect Union(const LayerRect &rect1, const LayerRect &rect2) {
  LayerRect res;

  if (!IsValid(rect1) && !IsValid(rect2)) {
    return LayerRect();
  }

  if (!IsValid(rect1)) {
    return rect2;
  }

  if (!IsValid(rect2)) {
    return rect1;
  }

  res.left = std::min(rect1.left, rect2.left);
  res.top = std::min(rect1.top, rect2.top);
  res.right = std::max(rect1.right, rect2.right);
  res.bottom = std::max(rect1.bottom, rect2.bottom);

  return res;
}

void SplitLeftRight(const LayerRect &in_rect, uint32_t split_count, uint32_t align_x,
                    bool flip_horizontal, LayerRect *out_rects) {
  LayerRect rect_temp = in_rect;

  uint32_t split_width = UINT32(rect_temp.right - rect_temp.left) / split_count;
  float aligned_width = FLOAT(CeilToMultipleOf(split_width, align_x));

  for (uint32_t count = 0; count < split_count; count++) {
    float aligned_right = rect_temp.left + aligned_width;
    out_rects[count].left = rect_temp.left;
    out_rects[count].right = std::min(rect_temp.right, aligned_right);
    out_rects[count].top = rect_temp.top;
    out_rects[count].bottom = rect_temp.bottom;

    rect_temp.left = out_rects[count].right;

    Log(kTagRotator, "SplitLeftRight", out_rects[count]);
  }

  // If we have a horizontal flip, then we should be splitting the source from right to left
  // to ensure that the right split will have an aligned width that matches the alignment on the
  // destination.
  if (flip_horizontal && split_count > 1) {
    out_rects[0].right = out_rects[0].left + (out_rects[1].right - out_rects[1].left);
    out_rects[1].left = out_rects[0].right;
    Log(kTagRotator, "Adjusted Left", out_rects[0]);
    Log(kTagRotator, "Adjusted Right", out_rects[1]);
  }
}

void SplitTopBottom(const LayerRect &in_rect, uint32_t split_count, uint32_t align_y,
                    bool flip_horizontal, LayerRect *out_rects) {
  LayerRect rect_temp = in_rect;

  uint32_t split_height = UINT32(rect_temp.bottom - rect_temp.top) / split_count;
  float aligned_height = FLOAT(CeilToMultipleOf(split_height, align_y));

  for (uint32_t count = 0; count < split_count; count++) {
    float aligned_bottom = rect_temp.top + aligned_height;
    out_rects[count].top = rect_temp.top;
    out_rects[count].bottom = std::min(rect_temp.bottom, aligned_bottom);
    out_rects[count].left = rect_temp.left;
    out_rects[count].right = rect_temp.right;

    rect_temp.top = out_rects[count].bottom;

    Log(kTagRotator, "SplitTopBottom", out_rects[count]);
  }

  // If we have a horizontal flip, then we should be splitting the destination from bottom to top
  // to ensure that the bottom split's y-offset is aligned correctly after we swap the destinations
  // while accounting for the flip.
  if (flip_horizontal && split_count > 1) {
    out_rects[0].bottom = out_rects[0].top + (out_rects[1].bottom - out_rects[1].top);
    out_rects[1].top = out_rects[0].bottom;
    Log(kTagRotator, "Adjusted Top", out_rects[0]);
    Log(kTagRotator, "Adjusted Bottom", out_rects[1]);
  }
}

void MapRect(const LayerRect &src_domain, const LayerRect &dst_domain, const LayerRect &in_rect,
             LayerRect *out_rect) {
  if (!IsValid(src_domain) || !IsValid(dst_domain) || !IsValid(in_rect)) {
    return;
  }

  int x_offset = INT(src_domain.left);
  int y_offset = INT(src_domain.top);

  LayerRect modified_in_rect = Reposition(in_rect, -x_offset, -y_offset);
  float src_domain_width = src_domain.right - src_domain.left;
  float src_domain_height = src_domain.bottom - src_domain.top;
  float dst_domain_width = dst_domain.right - dst_domain.left;
  float dst_domain_height = dst_domain.bottom - dst_domain.top;

  float width_ratio = dst_domain_width / src_domain_width;
  float height_ratio = dst_domain_height / src_domain_height;

  out_rect->left = dst_domain.left + (width_ratio * modified_in_rect.left);
  out_rect->top = dst_domain.top + (height_ratio * modified_in_rect.top);
  out_rect->right = dst_domain.left + (width_ratio * modified_in_rect.right);
  out_rect->bottom = dst_domain.top + (height_ratio * modified_in_rect.bottom);
}

void TransformHV(const LayerRect &src_domain, const LayerRect &in_rect,
                 const LayerTransform &transform, LayerRect *out_rect) {
  if (!IsValid(src_domain) || !IsValid(in_rect)) {
    return;
  }

  float in_width = in_rect.right - in_rect.left;
  float in_height = in_rect.bottom - in_rect.top;
  float x_offset = in_rect.left - src_domain.left;
  float y_offset = in_rect.top - src_domain.top;
  *out_rect = in_rect;

  if (transform.flip_horizontal) {
    out_rect->right = src_domain.right - x_offset;
    out_rect->left = out_rect->right - in_width;
  }

  if (transform.flip_vertical) {
    out_rect->bottom = src_domain.bottom - y_offset;
    out_rect->top = out_rect->bottom - in_height;
  }
}

RectOrientation GetOrientation(const LayerRect &in_rect) {
  if (!IsValid(in_rect)) {
    return kOrientationUnknown;
  }

  float input_width = in_rect.right - in_rect.left;
  float input_height = in_rect.bottom - in_rect.top;

  if (input_width < input_height) {
    return kOrientationPortrait;
  }

  return kOrientationLandscape;
}

}  // namespace sdm

