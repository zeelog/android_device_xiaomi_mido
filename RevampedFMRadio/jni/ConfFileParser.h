/*
Copyright (c) 2015, The Linux Foundation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name of The Linux Foundation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef __CONF_FILE_PARSER_H__
#define __CONF_FILE_PARSER_H__

#define MAX_LINE_LEN 512
#define MAX_UNIQ_KEYS 5
#define MAX_UNIQ_GRPS 10
#define TRUE 1
#define FALSE 0

struct key_value_pair_list
{
   char *key;
   char *value;
   key_value_pair_list *next;
};

struct group
{
    char *grp_name;
    unsigned int num_of_keys;
    unsigned int keys_hash_size;
    key_value_pair_list **list;
    group *grp_next;
};

struct group_table
{
    unsigned int grps_hash_size;
    unsigned int num_of_grps;
    group **grps_hash;
};

enum CONF_PARSE_ERRO_CODE
{
  PARSE_SUCCESS,
  INVALID_FILE_NAME,
  FILE_OPEN_FAILED,
  FILE_NOT_PROPER,
  MEMORY_ALLOC_FAILED,
  PARSE_FAILED,
};

unsigned int get_hash_code(const char *str);
group_table *get_key_file();
void free_strs(char **str_array);
void free_key_file(group_table *key_file);
char parse_load_file(group_table *key_file, const char *file);
char **get_grps(const group_table *key_file);
char **get_keys(const group_table *key_file, const char *grp);
char *get_value(const group_table *key_file, const char *grp,
                 const char *key);

#endif //__CONF_FILE_PARSER_H__
