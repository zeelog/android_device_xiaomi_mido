/* Copyright (c) 2014, 2020 The Linux Foundation. All rights reserved.
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
 *     * Neither the name of The Linux Foundation, nor the names of its
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
 *
 */
#ifndef _LOC_MISC_UTILS_H_
#define _LOC_MISC_UTILS_H_
#include <stdint.h>
#include <ios>
#include <string>
#include <sstream>

#ifdef __cplusplus
extern "C" {
#endif
#include <stddef.h>
#include <stdint.h>
/*===========================================================================
FUNCTION loc_split_string

DESCRIPTION:
    This function is used to split a delimiter separated string into
    sub-strings. This function does not allocate new memory to store the split
    strings. Instead, it places '\0' in places of delimiters and assings the
    starting address of the substring within the raw string as the string address
    The input raw_string no longer remains to be a collection of sub-strings
    after this function is executed.
    Please make a copy of the input string before calling this function if
    necessary

PARAMETERS:
    char *raw_string: is the original string with delimiter separated substrings
    char **split_strings_ptr: is the arraw of pointers which will hold the addresses
                              of individual substrings
    int max_num_substrings: is the maximum number of substrings that are expected
                            by the caller. The array of pointers in the above parameter
                            is usually this long
    char delimiter: is the delimiter that separates the substrings. Examples: ' ', ';'

DEPENDENCIES
    N/A

RETURN VALUE
    int Number of split strings

SIDE EFFECTS
    The input raw_string no longer remains a delimiter separated single string.

EXAMPLE
    delimiter = ' ' //space
    raw_string = "hello new user" //delimiter is space ' '
    addresses  =  0123456789abcd
    split_strings_ptr[0] = &raw_string[0]; //split_strings_ptr[0] contains "hello"
    split_strings_ptr[1] = &raw_string[6]; //split_strings_ptr[1] contains "new"
    split_strings_ptr[2] = &raw_string[a]; //split_strings_ptr[2] contains "user"

===========================================================================*/
int loc_util_split_string(char *raw_string, char **split_strings_ptr, int max_num_substrings,
                     char delimiter);

/*===========================================================================
FUNCTION trim_space

DESCRIPTION
   Removes leading and trailing spaces of the string

DEPENDENCIES
   N/A

RETURN VALUE
   None

SIDE EFFECTS
   N/A
===========================================================================*/
void loc_util_trim_space(char *org_string);

/*===========================================================================
FUNCTION dlGetSymFromLib

DESCRIPTION
   Handy function to get a pointer to a symbol from a library.

   If libHandle is not null, it will be used as the handle to the library. In
   that case libName wll not be used;
   libHandle is an in / out parameter.
   If libHandle is null, libName will be used to dlopen.
   Either libHandle or libName must not be nullptr.
   symName must not be null.

DEPENDENCIES
   N/A

RETURN VALUE
   pointer to symName. Could be nullptr if
       Parameters are incorrect; or
       libName can not be opened; or
       symName can not be found.

SIDE EFFECTS
   N/A
===========================================================================*/
void* dlGetSymFromLib(void*& libHandle, const char* libName, const char* symName);

/*===========================================================================
FUNCTION getQTimerTickCount

DESCRIPTION
   This function is used to read the QTimer ticks count. This value is globally maintained and
   must be the same across all processors on a target.

DEPENDENCIES
   N/A

RETURN VALUE
    uint64_t QTimer tick count

SIDE EFFECTS
   N/A
===========================================================================*/
uint64_t getQTimerTickCount();

/*===========================================================================
FUNCTION getQTimerDeltaNanos

DESCRIPTION
This function is used to read the the difference in nanoseconds between
Qtimer on AP side and Qtimer on MP side for dual-SoC architectures such as Kona

DEPENDENCIES
N/A

RETURN VALUE
uint64_t QTimer difference in nanoseconds

SIDE EFFECTS
N/A
===========================================================================*/
uint64_t getQTimerDeltaNanos();

/*===========================================================================
FUNCTION getQTimerFreq

DESCRIPTION
   This function is used to read the QTimer frequency in hz. This value is globally maintained and
   must be the same across all processors on a target.

DEPENDENCIES
   N/A

RETURN VALUE
    uint64_t QTimer frequency

SIDE EFFECTS
   N/A
===========================================================================*/
uint64_t getQTimerFreq();

/*===========================================================================
FUNCTION getBootTimeMilliSec

DESCRIPTION
   This function is used to get boot time in milliseconds.

DEPENDENCIES
   N/A

RETURN VALUE
    uint64_t boot time in milliseconds

SIDE EFFECTS
   N/A
===========================================================================*/
uint64_t getBootTimeMilliSec();

#ifdef __cplusplus
}
#endif

using std::hex;
using std::string;
using std::stringstream;

/*===========================================================================
FUNCTION to_string_hex

DESCRIPTION
   This function works similar to std::to_string, but puts only in hex format.

DEPENDENCIES
   N/A

RETURN VALUE
   string, of input val in hex format

SIDE EFFECTS
   N/A
===========================================================================*/
template <typename T>
string to_string_hex(T val) {
    stringstream ss;
    if (val < 0) {
        val = -val;
        ss << "-";
    }
    ss << hex << "0x" << val;
    return ss.str();
}

/*===========================================================================
FUNCTION loc_prim_arr_to_string

DESCRIPTION
   This function puts out primitive array in DEC or EHX format.

DEPENDENCIES
   N/A

RETURN VALUE
    string, space separated string of values in the input array, either
            in decimal or hex format, depending on the value of decIfTrue

SIDE EFFECTS
   N/A
===========================================================================*/
template <typename T>
static string loc_prim_arr_to_string(T* arr, uint32_t size, bool decIfTrue = true) {
    stringstream ss;
    for (uint32_t i = 0; i < size; i++) {
        ss << (decIfTrue ? to_string(arr[i]) : to_string_hex(arr[i]));
        if (i != size - 1) {
            ss << " ";
        }
    }
    return ss.str();
}

/*===========================================================================
FUNCTION qTimerTicksToNanos

DESCRIPTION
    Transform from ticks to nanoseconds, clock is 19.2 MHz
    so the formula would be qtimer(ns) = (ticks * 1000000000) / 19200000
    or simplified qtimer(ns) = (ticks * 10000) / 192.

DEPENDENCIES
    N/A

RETURN VALUE
    Qtimer value in nanoseconds

SIDE EFFECTS
    N/A
===========================================================================*/
inline uint64_t qTimerTicksToNanos(double qTimer) {
    return (uint64_t((qTimer * double(10000ull)) / (double)192ull));
}

/*===========================================================================
FUNCTION loc_convert_lla_gnss_to_vrp

DESCRIPTION
   This function converts lat/long/altitude from GNSS antenna based
   to vehicle reference point based.

DEPENDENCIES
   N/A

RETURN VALUE
    The converted lat/long/altitude will be stored in the parameter of llaInfo.

SIDE EFFECTS
   N/A
===========================================================================*/
void loc_convert_lla_gnss_to_vrp(double lla[3], float rollPitchYaw[3],
                                 float leverArm[3]);

/*===========================================================================
FUNCTION loc_convert_velocity_gnss_to_vrp

DESCRIPTION
   This function converts east/north/up velocity from GNSS antenna based
   to vehicle reference point based.

DEPENDENCIES
   N/A

RETURN VALUE
    The converted east/north/up velocity will be stored in the parameter of
    enuVelocity.

SIDE EFFECTS
   N/A
===========================================================================*/
void loc_convert_velocity_gnss_to_vrp(float enuVelocity[3], float rollPitchYaw[3],
                                      float rollPitchYawRate[3], float leverArm[3]);

#endif //_LOC_MISC_UTILS_H_
