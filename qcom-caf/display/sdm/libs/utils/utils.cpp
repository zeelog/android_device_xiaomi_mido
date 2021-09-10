/*
* Copyright (c) 2016 - 2017, The Linux Foundation. All rights reserved.
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

#include <unistd.h>
#include <math.h>
#include <utils/sys.h>
#include <utils/utils.h>

#include <algorithm>

#define __CLASS__ "Utils"

namespace sdm {

float gcd(float a, float b) {
  if (a < b) {
    std::swap(a, b);
  }

  while (b != 0) {
    float tmp = b;
    b = fmodf(a, b);
    a = tmp;
  }

  return a;
}

float lcm(float a, float b) {
  return (a * b) / gcd(a, b);
}

void CloseFd(int *fd) {
  if (*fd >= 0) {
    Sys::close_(*fd);
    *fd = -1;
  }
}

DriverType GetDriverType() {
    const char *fb_caps = "/sys/devices/virtual/graphics/fb0/mdp/caps";
    // 0 - File exists
    return Sys::access_(fb_caps, F_OK) ? DriverType::DRM : DriverType::FB;
}
}  // namespace sdm
