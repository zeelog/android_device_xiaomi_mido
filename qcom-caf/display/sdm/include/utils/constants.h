/*
* Copyright (c) 2014 - 2017, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without modification, are permitted
* provided that the following conditions are met:
*    * Redistributions of source code must retain the above copyright notice, this list of
*      conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above copyright notice, this list of
*      conditions and the following disclaimer in the documentation and/or other materials provided
*      with the distribution.
*    * Neither the name of The Linux Foundation nor the names of its contributors may be used to
*      endorse or promote products derived from this software without specific prior written
*      permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
* STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef __CONSTANTS_H__
#define __CONSTANTS_H__

#include <stdlib.h>
#include <inttypes.h>

#ifndef PRIu64
#define PRIu64 "llu"
#endif

#define INT(exp) static_cast<int>(exp)
#define FLOAT(exp) static_cast<float>(exp)
#define UINT8(exp) static_cast<uint8_t>(exp)
#define UINT16(exp) static_cast<uint16_t>(exp)
#define UINT32(exp) static_cast<uint32_t>(exp)
#define INT32(exp) static_cast<int32_t>(exp)
#define UINT64(exp) static_cast<uint64_t>(exp)

#define ROUND_UP(number, step) ((((number) + ((step) - 1)) / (step)) * (step))

#define BITMAP(bit) (1 << (bit))

#define ROUND_UP_ALIGN_DOWN(value, a) FLOAT(FloorToMultipleOf(UINT32(value + 0.5f), UINT32(a)))
#define ROUND_UP_ALIGN_UP(value, a) FLOAT(CeilToMultipleOf(UINT32(value + 0.5f), UINT32(a)))

#define IDLE_TIMEOUT_DEFAULT_MS 70
#define IDLE_TIMEOUT_ACTIVE_MS IDLE_TIMEOUT_DEFAULT_MS
#define IDLE_TIMEOUT_INACTIVE_MS 520

#define IS_RGB_FORMAT(format) (((format) < kFormatYCbCr420Planar) ? true: false)

#define BITS_PER_BYTE 8
#define BITS_TO_BYTES(x) (((x) + (BITS_PER_BYTE - 1)) / (BITS_PER_BYTE))

// factor value should be in powers of 2(eg: 1, 2, 4, 8)
template <class T1, class T2>
inline T1 FloorToMultipleOf(const T1 &value, const T2 &factor) {
  return (T1)(value & (~(factor - 1)));
}

template <class T1, class T2>
inline T1 CeilToMultipleOf(const T1 &value, const T2 &factor) {
  return (T1)((value + (factor - 1)) & (~(factor - 1)));
}

namespace sdm {

  const int kThreadPriorityUrgent = -9;
  const int kMaxRotatePerLayer = 2;
  const uint32_t kMaxBlitTargetLayers = 2;
  const int kPageSize = 4096;
  const uint32_t kGridSize = 129;  // size used for non-linear transformation before Tone-mapping
  const uint32_t kLutDim = 17;  // Dim of the 3d LUT for tone-mapping.

  typedef void * Handle;

}  // namespace sdm

#endif  // __CONSTANTS_H__

