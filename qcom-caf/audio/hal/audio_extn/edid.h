/*
 * Copyright (c) 2014, 2016-2017, The Linux Foundation. All rights reserved.
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

#ifndef EDID_H
#define EDID_H

/* HDMI EDID Information */
#define BIT(nr)     (1UL << (nr))
#define MAX_EDID_BLOCKS 10
#define MAX_SHORT_AUDIO_DESC_CNT        30
#define MIN_AUDIO_DESC_LENGTH           3
#define MIN_SPKR_ALLOCATION_DATA_LENGTH 3
#define MAX_CHANNELS_SUPPORTED          8
#define MAX_DISPLAY_DEVICES             3
#define MAX_FRAME_BUFFER_NAME_SIZE      80
#define MAX_CHAR_PER_INT                13

#define PCM_CHANNEL_FL    1  /* Front left channel.                           */
#define PCM_CHANNEL_FR    2  /* Front right channel.                          */
#define PCM_CHANNEL_FC    3  /* Front center channel.                         */
#define PCM_CHANNEL_LS    4  /* Left surround channel.                        */
#define PCM_CHANNEL_RS    5  /* Right surround channel.                       */
#define PCM_CHANNEL_LFE   6  /* Low frequency effect channel.                 */
#define PCM_CHANNEL_CS    7  /* Center surround channel; Rear center channel. */
#define PCM_CHANNEL_LB    8  /* Left back channel; Rear left channel.         */
#define PCM_CHANNEL_RB    9  /* Right back channel; Rear right channel.       */
#define PCM_CHANNEL_TS   10  /* Top surround channel.                         */
#define PCM_CHANNEL_CVH  11  /* Center vertical height channel.               */
#define PCM_CHANNEL_MS   12  /* Mono surround channel.                        */
#define PCM_CHANNEL_FLC  13  /* Front left of center.                         */
#define PCM_CHANNEL_FRC  14  /* Front right of center.                        */
#define PCM_CHANNEL_RLC  15  /* Rear left of center.                          */
#define PCM_CHANNEL_RRC  16  /* Rear right of center.                         */
#define PCM_CHANNEL_LFE2 17  /* Second low frequency channel.                 */
#define PCM_CHANNEL_SL   18  /* Side left channel.                            */
#define PCM_CHANNEL_SR   19  /* Side right channel.                           */
#define PCM_CHANNEL_TFL  20  /* Top front left channel.                       */
#define PCM_CHANNEL_LVH  20  /* Left vertical height channel.                 */
#define PCM_CHANNEL_TFR  21  /* Top front right channel.                      */
#define PCM_CHANNEL_RVH  21  /* Right vertical height channel.                */
#define PCM_CHANNEL_TC   22  /* Top center channel.                           */
#define PCM_CHANNEL_TBL  23  /* Top back left channel.                        */
#define PCM_CHANNEL_TBR  24  /* Top back right channel.                       */
#define PCM_CHANNEL_TSL  25  /* Top side left channel.                        */
#define PCM_CHANNEL_TSR  26  /* Top side right channel.                       */
#define PCM_CHANNEL_TBC  27  /* Top back center channel.                      */
#define PCM_CHANNEL_BFC  28  /* Bottom front center channel.                  */
#define PCM_CHANNEL_BFL  29  /* Bottom front left channel.                    */
#define PCM_CHANNEL_BFR  30  /* Bottom front right channel.                   */
#define PCM_CHANNEL_LW   31  /* Left wide channel.                            */
#define PCM_CHANNEL_RW   32  /* Right wide channel.                           */
#define PCM_CHANNEL_LSD  33  /* Left side direct channel.                     */
#define PCM_CHANNEL_RSD  34  /* Right side direct channel.                    */

#define MAX_HDMI_CHANNEL_CNT 8

typedef enum edid_audio_format_id {
    LPCM = 1,
    AC3,
    MPEG1,
    MP3,
    MPEG2_MULTI_CHANNEL,
    AAC,
    DTS,
    ATRAC,
    SACD,
    DOLBY_DIGITAL_PLUS,
    DTS_HD,
    MAT,
    DST,
    WMA_PRO
} edid_audio_format_id;

typedef struct edid_audio_block_info {
    edid_audio_format_id format_id;
    int sampling_freq_bitmask;
    int bits_per_sample_bitmask;
    int channels;
} edid_audio_block_info;

typedef struct edid_audio_info {
    int audio_blocks;
    unsigned char speaker_allocation[MIN_SPKR_ALLOCATION_DATA_LENGTH];
    edid_audio_block_info audio_blocks_array[MAX_EDID_BLOCKS];
    char channel_map[MAX_CHANNELS_SUPPORTED];
    int  channel_allocation;
    unsigned int  channel_mask;
} edid_audio_info;

bool edid_is_supported_sr(edid_audio_info* info, int sr);
bool edid_is_supported_bps(edid_audio_info* info, int bps);
int edid_get_highest_supported_sr(edid_audio_info* info);
bool edid_get_sink_caps(edid_audio_info* info, char *edid_data);
#endif /* EDID_H */
