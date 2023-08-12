/* Copyright (c) 2011-2012, 2015, 2020 The Linux Foundation. All rights reserved.
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

#ifndef LOC_LOG_H
#define LOC_LOG_H

#include <ctype.h>
#include <stdlib.h>
#include <unordered_map>
#include <string>
#include "loc_target.h"
#include "loc_misc_utils.h"

using std::string;
using std::unordered_map;

typedef unordered_map<int64_t, string> NameValTbl;

#define NAME_VAL(x) {x, "" #x ""}
#define DECLARE_TBL(T) static const NameValTbl T##_tbl

extern const string gEmptyStr;
extern const string gUnknownStr;

#define CHECK_MASK(type, value, mask_var, mask) \
   (((mask_var) & (mask)) ? (type) (value) : (type) (-1))

#define LOC_TABLE_SIZE(table) (sizeof(table)/sizeof((table)[0]))

#define FIELDVAL_DEC(field) \
        loc_put_tag_val(#field, to_string(field))
#define FIELDVAL_DEC_ARR(field) \
        loc_put_tag_val(#field, \
                        loc_parenthesize(loc_prim_arr_to_string(field, \
                                                                sizeof(field)/sizeof(field[0]))))
#define FIELDVAL_HEX(field) \
        loc_put_tag_val(#field, to_string_hex(field))
#define FIELDVAL_HEX_ARR(field) \
        loc_put_tag_val(#field, \
                        loc_parenthesize(loc_prim_arr_to_string(field, \
                                                                sizeof(field)/sizeof(field[0]), \
                                                                false)))
#define FIELDVAL_ENUM(field, tbl) \
        loc_put_tag_val(#field, \
                        loc_get_name_from_tbl(tbl, field, gUnknownStr))
#define FIELDVAL_MASK(field, tbl) \
        loc_put_tag_val(#field, \
                        to_string_hex((uint64_t)field) + " " + \
                                loc_parenthesize(loc_get_bit_defs(field, tbl)))

/* get from a table of strings with index */
/* tbl - map of <int, string> entries
   key - key to the matching entry
   defalt - default pointer in case of incorrect parameters
 */
inline static const string& loc_get_name_from_tbl(const NameValTbl& tbl, int64_t key,
                                    const string& defalt = gEmptyStr) {
    auto item = tbl.find(key);
    if (item != tbl.end()) {
        return item->second;
    } else {
        return defalt;
    }
}

/* puts to string formatted "TAG: VAL" with option ending string, default to newline */
inline string loc_put_tag_val(const string& tag, const string& val, const string& eol = "\n") {
    return tag + ": " + val + eol;
}

inline string loc_parenthesize(const string& str) {
    return "(" + str + ")";
}

/* Get names from value */
inline const char* loc_get_name_from_val(const NameValTbl& table, int64_t value) {
    return loc_get_name_from_tbl(table, value, gUnknownStr).c_str();
}

inline const char* log_succ_fail_string(int is_succ) {
    return is_succ? "successful" : "failed";
}

/* prints mask into a string with bit definitions from tbl */
/* mask - bit mask, to be expanded into " BIT_NAMEx | BIT_NAMEy ... "
   tbl - a table with defs for each bit, defined as <bit, name> entries
         {{bit0, "BIT0_NAME"}, {bit1, "BIT1_NAME"}, .... {bitn, "BITn_NAME"} }
   entries - number of strings in the table
 */
string loc_get_bit_defs(uint64_t mask, const NameValTbl& tbl);
uint64_t loc_get_least_bit(uint64_t& mask, bool clearThebit = true);
const char* loc_get_msg_q_status(int status);
const char* loc_get_target_name(unsigned int target);
char *loc_get_time(char *time_string, size_t buf_size);

#endif /* LOC_LOG_H */
