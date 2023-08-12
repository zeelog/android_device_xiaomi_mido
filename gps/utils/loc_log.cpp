/* Copyright (c) 2011-2012, 2015, 2020, The Linux Foundation. All rights reserved.
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

#define LOG_NDEBUG 0

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include "log_util.h"
#include "loc_log.h"
#include "msg_q.h"
#include <loc_pla.h>
#include "LogBuffer.h"
#include <unordered_map>
#include <fstream>
#include <algorithm>
#include <string>
#include <cctype>
#define  BUFFER_SIZE  120
#define  LOG_TAG_LEVEL_CONF_FILE_PATH "/data/vendor/location/gps.prop"

// Logging Improvements
const char *loc_logger_boolStr[]={"False","True"};
const char VOID_RET[]   = "None";
const char FROM_AFW[]   = "===>";
const char TO_MODEM[]   = "--->";
const char FROM_MODEM[] = "<---";
const char TO_AFW[]     = "<===";
const char EXIT_TAG[]   = "Exiting";
const char ENTRY_TAG[]  = "Entering";
const char EXIT_ERROR_TAG[]  = "Exiting with error";

int build_type_prop = BUILD_TYPE_PROP_NA;

const string gEmptyStr = "";
const string gUnknownStr = "UNKNOWN";
/* Logging Mechanism */
loc_logger_s_type loc_logger;

/* tag base logging control map*/
static std::unordered_map<std::string, uint8_t> tag_level_map;
static bool tag_map_inited = false;

/* returns the least signification bit that is set in the mask
   Param
      mask -        bit mask.
      clearTheBit - if true, mask gets modified upon return.
   returns 0 if mask is 0.
*/
uint64_t loc_get_least_bit(uint64_t& mask, bool clearTheBit) {
    uint64_t bit = 0;

    if (mask > 0) {
        uint64_t less1 = mask - 1;
        bit = mask & ~(less1);
        if (clearTheBit) {
            mask &= less1;
        }
    }

    return bit;
}

string loc_get_bit_defs(uint64_t mask, const NameValTbl& tbl) {
    string out;
    while (mask > 0) {
        out += loc_get_name_from_tbl(tbl, loc_get_least_bit(mask));
        if (mask > 0) {
            out += " | ";
        }
    }
    return out;
}

DECLARE_TBL(loc_msg_q_status) =
{
    NAME_VAL( eMSG_Q_SUCCESS ),
    NAME_VAL( eMSG_Q_FAILURE_GENERAL ),
    NAME_VAL( eMSG_Q_INVALID_PARAMETER ),
    NAME_VAL( eMSG_Q_INVALID_HANDLE ),
    NAME_VAL( eMSG_Q_UNAVAILABLE_RESOURCE ),
    NAME_VAL( eMSG_Q_INSUFFICIENT_BUFFER )
};

/* Find msg_q status name */
const char* loc_get_msg_q_status(int status)
{
   return loc_get_name_from_val(loc_msg_q_status_tbl, (int64_t) status);
}

//Target names
DECLARE_TBL(target_name) =
{
    NAME_VAL(GNSS_NONE),
    NAME_VAL(GNSS_MSM),
    NAME_VAL(GNSS_GSS),
    NAME_VAL(GNSS_MDM),
    NAME_VAL(GNSS_AUTO),
    NAME_VAL(GNSS_UNKNOWN)
};

/*===========================================================================

FUNCTION loc_get_target_name

DESCRIPTION
   Returns pointer to a string that contains name of the target

   XX:XX:XX.000\0

RETURN VALUE
   The target name string

===========================================================================*/
const char *loc_get_target_name(unsigned int target)
{
    int64_t index = 0;
    static char ret[BUFFER_SIZE];

    snprintf(ret, sizeof(ret), " %s with%s SSC",
             loc_get_name_from_val(target_name_tbl, getTargetGnssType(target)),
             ((target & HAS_SSC) == HAS_SSC) ? gEmptyStr.c_str() : "out");

    return ret;
}


/*===========================================================================

FUNCTION loc_get_time

DESCRIPTION
   Logs a callback event header.
   The pointer time_string should point to a buffer of at least 13 bytes:

   XX:XX:XX.000\0

RETURN VALUE
   The time string

===========================================================================*/
char *loc_get_time(char *time_string, size_t buf_size)
{
   struct timeval now;     /* sec and usec     */
   struct tm now_tm;       /* broken-down time */
   char hms_string[80];    /* HH:MM:SS         */

   gettimeofday(&now, NULL);
   localtime_r(&now.tv_sec, &now_tm);

   strftime(hms_string, sizeof hms_string, "%H:%M:%S", &now_tm);
   snprintf(time_string, buf_size, "%s.%03d", hms_string, (int) (now.tv_usec / 1000));

   return time_string;
}

/*===========================================================================
FUNCTION get_timestamp

DESCRIPTION
   Generates a timestamp using the current system time

DEPENDENCIES
   N/A

RETURN VALUE
   Char pointer to the parameter str

SIDE EFFECTS
   N/A
===========================================================================*/
char * get_timestamp(char *str, unsigned long buf_size)
{
  struct timeval tv;
  struct timezone tz;
  int hh, mm, ss;
  gettimeofday(&tv, &tz);
  hh = tv.tv_sec/3600%24;
  mm = (tv.tv_sec%3600)/60;
  ss = tv.tv_sec%60;
  snprintf(str, buf_size, "%02d:%02d:%02d.%06ld", hh, mm, ss, tv.tv_usec);
  return str;
}

/*===========================================================================

FUNCTION log_buffer_insert

DESCRIPTION
   Insert a log sentence with specific level to the log buffer.

RETURN VALUE
   N/A

===========================================================================*/
void log_buffer_insert(char *str, unsigned long buf_size, int level)
{
    timespec tv;
    clock_gettime(CLOCK_BOOTTIME, &tv);
    uint64_t elapsedTime = (uint64_t)tv.tv_sec + (uint64_t)tv.tv_nsec/1000000000;
    string ss = str;
    loc_util::LogBuffer::getInstance()->append(ss, level, elapsedTime);
}

void log_tag_level_map_init()
{
    if (tag_map_inited) {
        return;
    }

    std::string filename = LOG_TAG_LEVEL_CONF_FILE_PATH;

    std::ifstream s(filename);
    if (!s.is_open()) {
        ALOGE("cannot open file:%s", LOG_TAG_LEVEL_CONF_FILE_PATH);
    } else {
        std::string line;
        while (std::getline(s, line)) {
            line.erase(std::remove(line.begin(), line.end(), ' '), line.end());
            int pos = line.find('=');
            if (pos <= 0 || pos >= (line.size() - 1)) {
                ALOGE("wrong format in gps.prop");
                continue;
            }
            std::string tag = line.substr(0, pos);
            std::string level = line.substr(pos+1, 1);
            if (!std::isdigit(*(level.begin()))) {
                ALOGE("wrong format in gps.prop");
                continue;
            }
            tag_level_map[tag] = (uint8_t)std::stoul(level);
        }
    }
    tag_map_inited = true;
}

int get_tag_log_level(const char* tag)
{
    if (!tag_map_inited) {
        return -1;
    }

    // in case LOG_TAG isn't defined in a source file, use the global log level
    if (tag == NULL) {
        return loc_logger.DEBUG_LEVEL;
    }
    int log_level;
    auto search = tag_level_map.find(std::string(tag));
    if (tag_level_map.end() != search) {
        log_level = search->second;
    } else {
        log_level = loc_logger.DEBUG_LEVEL;
    }
    return log_level;
}
