/*
* Copyright (c) 2016, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
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

#ifndef AUDIO_HW_EXTN_API_H
#define AUDIO_HW_EXTN_API_H
__BEGIN_DECLS
#ifdef AUDIO_HW_EXTN_API_ENABLED
#include <hardware/audio.h>
typedef struct qahwi_stream_in qahwi_stream_in_t;
typedef struct qahwi_stream_out qahwi_stream_out_t;
typedef struct qahwi_device qahwi_device_t;

struct qahwi_stream_in {
    struct audio_stream_in base;
    bool is_inititalized;
    void *ibuf;
};

struct qahwi_stream_out {
    struct audio_stream_out base;
    bool is_inititalized;
    size_t buf_size;
    void *obuf;
};

struct qahwi_device {
    struct audio_hw_device base;
    bool is_inititalized;
};

void qahwi_init(hw_device_t *device);
void qahwi_deinit(hw_device_t *device);
#else
typedef void *qahwi_stream_in_t;
typedef void *qahwi_stream_out_t;
typedef void *qahwi_device_t;

#define qahwi_init(device) (0)
#define qahwi_deinit(device) (0)
#endif  // AUDIO_HW_EXTN_API_ENABLED

__END_DECLS
#endif  // AUDIO_HW_EXTN_API_H
