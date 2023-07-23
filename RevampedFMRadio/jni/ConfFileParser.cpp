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

#define LOG_TAG "android_hardware_fm"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <utils/Log.h>
#include "ConfFileParser.h"

//declaration of functions only specific to this file
static char parse_line
(
  group_table *key_file,
  const char *line,
  char **cur_grp
);

static char parse_load_frm_fhandler
(
  group_table *key_file,
  FILE *fp
);

static char line_is_grp
(
  group_table *key_file,
  const char *str,
  char **cur_grp
);

static void free_grp_list
(
  group *a
);

static void free_key_list
(
  key_value_pair_list *a
);

static char line_is_key_value_pair
(
  group_table *key_file,
  const char *str,
  const char *cur_grp
);

static char line_is_comment
(
  const char *str
);

static char grp_exist
(
  const group_table *key_file,
  const char *new_grp
);

static char add_grp
(
  group_table *key_file,
  const char *new_grp
);

static group *alloc_group
(
  void
);

static key_value_pair_list *alloc_key_value_pair
(
  void
);

static char add_key_value_pair
(
  group_table *key_file,
  const char *cur_grp,
  const char *key,
  const char *val
);


//Definitions
void free_strs
(
  char **str_array
)
{
  char **str_array_cpy = str_array;
  if(str_array != NULL) {
     while(*str_array != NULL) {
           free(*str_array);
           str_array++;
     }
  }
  free(str_array_cpy);
}
//ToDo: Come up with code hashing
//function
unsigned int get_hash_code
(
  const char *str
)
{

  unsigned len = strlen(str);
  unsigned int i;
  unsigned int hash_code = 0;

  for(i = 0; len > 0; len--, i++) {
      hash_code += (int)((str[i] * pow(2, len))) % INT_MAX;
      hash_code %= INT_MAX;
  }
  return hash_code;
}

static key_value_pair_list *alloc_key_value_pair
(
  void
)
{
  key_value_pair_list *key_list = NULL;

  key_list = (key_value_pair_list *)malloc(
                                       sizeof(key_value_pair_list));
  if(key_list != NULL) {
     key_list->key = NULL;
     key_list->next = NULL;
     key_list->value = NULL;
  }
  return key_list;
}

static group * alloc_group
(
  void
)
{
  group *grp = NULL;
  unsigned int i;

  grp = (group *)malloc(sizeof(group));
  if(grp != NULL) {
     grp->grp_name = NULL;
     grp->grp_next = NULL;
     grp->num_of_keys = 0;
     grp->keys_hash_size = MAX_UNIQ_KEYS;
     grp->list = (key_value_pair_list **)malloc
                    (sizeof(key_value_pair_list *) * grp->keys_hash_size);
     if(grp->list == NULL) {
        ALOGE("Could not alloc group\n");
        free(grp);
        grp = NULL;
     }else {
        for(i = 0; i < grp->keys_hash_size; i++) {
            grp->list[i] = NULL;
        }
     }
  }
  return grp;
}

group_table *get_key_file
(
)
{
  group_table *t = NULL;
  unsigned int i;

  t = (group_table *)malloc(sizeof(group_table));
  if (t != NULL) {
      t->grps_hash_size = MAX_UNIQ_GRPS;
      t->num_of_grps = 0;
      t->grps_hash = (group **)malloc(sizeof(group *)
                                       * t->grps_hash_size);
      if (t->grps_hash == NULL) {
          free(t);
          return NULL;
      }
      for(i = 0; i < t->grps_hash_size; i++) {
          t->grps_hash[i] = NULL;
      }
  }
  return t;
}

void free_key_file(
  group_table *key_file
)
{
  unsigned int i;

  if(key_file != NULL) {
     if(key_file->grps_hash != NULL) {
        for(i = 0; i < key_file->grps_hash_size; i++) {
            free_grp_list(key_file->grps_hash[i]);
        }
     }
     free(key_file->grps_hash);
     free(key_file);
  }
}

static void free_grp_list
(
  group *a
)
{
  group *next;
  unsigned int i;

  while(a != NULL) {
       next = a->grp_next;
       if(a->list != NULL) {
          for(i = 0; i < a->keys_hash_size; i++) {
              free_key_list(a->list[i]);
          }
       }
       free(a->grp_name);
       free(a->list);
       free(a);
       a = next;
  }
}

static void free_key_list
(
  key_value_pair_list *a
)
{
  key_value_pair_list *next;

  while(a != NULL) {
       next = a->next;
       free(a->key);
       free(a->value);
       free(a);
       a = next;
  }
}
//return all the groups
//present in the file
char **get_grps
(
  const group_table *key_file
)
{
  char **grps = NULL;
  unsigned int i = 0;
  unsigned int j = 0;
  unsigned int grp_len;
  group *grp_list;

  if((key_file == NULL)
     || (key_file->grps_hash == NULL)
     || (key_file->grps_hash_size == 0)
     || (key_file->num_of_grps == 0)) {
     return grps;
  }
  grps = (char **)calloc((key_file->num_of_grps + 1),
                           sizeof(char *));
  if(grps == NULL) {
     return grps;
  }
  for(i = 0; i < key_file->grps_hash_size; i++) {
      grp_list = key_file->grps_hash[i];
      while(grp_list != NULL) {
            grp_len = strlen(grp_list->grp_name);
            grps[j] = (char *)malloc(sizeof(char) *
                                     (grp_len + 1));
            if(grps[j] == NULL) {
               free_strs(grps);
               grps = NULL;
               return grps;
            }
            memcpy(grps[j], grp_list->grp_name,
                   (grp_len + 1));
            grp_list = grp_list->grp_next;
            j++;
      }
  }
  grps[j] = NULL;
  return grps;
}

//returns the list of keys
//associated with group name
char **get_keys
(
  const group_table *key_file,
  const char *grp_name
)
{
  unsigned int grp_hash_code;
  unsigned int grp_index;
  unsigned int i;
  unsigned int j = 0;
  unsigned int key_len;
  group *grp;
  key_value_pair_list *key_val_list;
  char **keys = NULL;

  if((key_file == NULL) || (grp_name == NULL)
     || (key_file->num_of_grps == 0) ||
     (key_file->grps_hash_size == 0) ||
     (key_file->grps_hash == NULL) ||
     (!strcmp(grp_name, ""))) {
      return keys;
  }
  grp_hash_code = get_hash_code(grp_name);
  grp_index = (grp_hash_code % key_file->grps_hash_size);
  grp = key_file->grps_hash[grp_index];
  while(grp != NULL) {
        if(!strcmp(grp_name, grp->grp_name)) {
            if((grp->num_of_keys == 0)
               || (grp->keys_hash_size == 0)
               || (grp->list == 0)) {
               return keys;
            }
            keys = (char **)calloc((grp->num_of_keys + 1),
                                   sizeof(char *));
            if(keys == NULL) {
                return keys;
            }
            for(i = 0; i < grp->keys_hash_size; i++) {
                key_val_list = grp->list[i];
                while(key_val_list != NULL) {
                     key_len = strlen(key_val_list->key);
                     keys[j] = (char *)malloc(sizeof(char) *
                                              (key_len + 1));
                     if(keys[j] == NULL) {
                         free_strs(keys);
                         keys = NULL;
                         return keys;
                     }
                     memcpy(keys[j], key_val_list->key,
                            (key_len + 1));
                     j++;
                     key_val_list = key_val_list->next;
                }
            }
            keys[j] = NULL;
            return keys;
        }
        grp = grp->grp_next;
  }
  return keys;
}

char *get_value
(
   const group_table *key_file,
   const char *grp_name,
   const char *key
)
{
   unsigned int grp_hash_code;
   unsigned int key_hash_code;
   unsigned int grp_index;
   unsigned int key_index;
   unsigned val_len;
   char *val = NULL;
   group *grp;
   key_value_pair_list *list;

   if((key_file == NULL) || (grp_name == NULL)
      || (key == NULL) || (key_file->grps_hash == NULL)
      || (key_file->grps_hash_size == 0) || !strcmp(grp_name, "")
      ||(!strcmp(key, ""))) {
       return NULL;
   }
   grp_hash_code = get_hash_code(grp_name);
   key_hash_code = get_hash_code(key);
   grp_index = (grp_hash_code % key_file->grps_hash_size);
   grp = key_file->grps_hash[grp_index];
   while(grp != NULL) {
         if(!strcmp(grp_name, grp->grp_name) && grp->keys_hash_size
            && grp->list) {
            key_index = (key_hash_code % grp->keys_hash_size);
            list = grp->list[key_index];
            while((list != NULL) && (strcmp(list->key, key))) {
                   list = list->next;
            }
            if(list != NULL) {
                val_len = strlen(list->value);
                val = (char *)malloc(sizeof(char) * (val_len + 1));
                if(val != NULL) {
                   memcpy(val, list->value, val_len);
                   val[val_len] = '\0';
                }
            }
            return val;
         }
         grp = grp->grp_next;
   }
   return val;
}
//open the file,
//read, parse and load
//returns PARSE_SUCCESS if successfully
//loaded else PARSE_FAILED
char parse_load_file
(
  group_table *key_file,
  const char *file
)
{
  FILE *fp;
  char ret = FALSE;

  if((file == NULL) || !strcmp(file, "")) {
     ALOGE("File name is null or empty \n");
     return ret;
  }

  fp = fopen(file, "r");
  if(fp == NULL) {
     ALOGE("could not open file for read\n");
     return ret;
  }

  ret = parse_load_frm_fhandler(key_file, fp);
  fclose(fp);

  return ret;
}

//Read block of data from file handler
//extract each line, check kind of line(comment,
//group, key value pair)
static char parse_load_frm_fhandler
(
  group_table *key_file,
  FILE *fp
)
{
  char buf[MAX_LINE_LEN];
  char ret = TRUE;
  char *line = NULL;
  void *new_line;
  char *cur_grp = NULL;
  unsigned line_len = 0;
  unsigned line_allocated = 0;
  unsigned int bytes_read = 0;
  unsigned int i;
  bool has_carriage_rtn = false;

  while ((bytes_read = fread(buf, 1, MAX_LINE_LEN, fp))) {
        for (i = 0; i < bytes_read; i++) {
            if (line_len == line_allocated) {
                line_allocated += 25;
                new_line = realloc(line, line_allocated);
                if (new_line == NULL) {
                   ret = FALSE;
                   ALOGE("memory allocation failed for line\n");
                   break;
                }
                line = (char *)new_line;
            }
            if (buf[i] == '\n') {
                has_carriage_rtn = false;
                line[line_len] = '\0';
                ret = parse_line(key_file, line, &cur_grp);
                line_len = 0;
                if(ret == FALSE) {
                   ALOGE("could not parse the line, line not proper\n");
                   break;
                }
            } else if (buf[i] == '\r') {
                ALOGE("File has carriage return\n");
                has_carriage_rtn = true;
            } else if (has_carriage_rtn) {
                ALOGE("File format is not proper, no line character\
                        after carraige return\n");
                ret = FALSE;
                break;
            } else {
                line[line_len] = buf[i];
                line_len++;
            }
        }
        if (!ret) {
            break;
        }
  }
  free(line);
  free(cur_grp);

  return ret;
}

//checks whether a line is
//comment or grp or key pair value
//and accordingly adds to list
static char parse_line
(
  group_table *key_file,
  const char *line,
  char **cur_grp
)
{
  const char *line_begin;

  if((line == NULL) || (key_file == NULL)) {
      ALOGE("key file or line is null\n");
      return FALSE;
  }

  for(line_begin = line; isspace(*line_begin);
          line_begin++);

  if(line_is_comment(line_begin)) {
      ALOGE("line is comment\n");
      return TRUE;
  }else if(line_is_grp(key_file, line_begin, cur_grp)) {
      ALOGE("line is grp\n");
      return TRUE;
  }else if(line_is_key_value_pair(key_file, line_begin, *cur_grp)) {
      ALOGE("line is key value pair\n");
      return TRUE;
  }else {
     ALOGE("line is neither comment, grp nor key value pair\n");
     return FALSE;
  }
}

static char line_is_comment
(
  const char *str
)
{
  if(str == NULL) {
      return FALSE;
  }else if(((*str) == '#') || ((*str) == '\0')
       || ((*str) == '\n')) {
      return TRUE;
  }else {
      ALOGE("line is not comment\n");
      return FALSE;
  }
}

//return true if a group
//name already exist
//else false
static char grp_exist
(
  const group_table *key_file,
  const char *new_grp
)
{
  unsigned hash_code;
  unsigned int index;
  group *grp;

  if((key_file == NULL) || (new_grp == NULL)
     || (!key_file->grps_hash_size)) {
     return FALSE;
  }else {
    hash_code = get_hash_code(new_grp);
    index = hash_code % key_file->grps_hash_size;
    grp = key_file->grps_hash[index];
    while(grp != NULL) {
          if (!strcmp(grp->grp_name, new_grp))
              return TRUE;
          grp = grp->grp_next;
    }
    return FALSE;
  }
}

//Add a group to group
//table if it does not exist
static char add_grp
(
  group_table *key_file,
  const char *new_grp
)
{
  unsigned int hash_code;
  unsigned int index;
  unsigned int grp_name_len;
  group *grp;

  if(!grp_exist(key_file, new_grp)) {
      if((key_file == NULL) || (new_grp == NULL)
         || !key_file->grps_hash_size) {
         return FALSE;
      }
      hash_code = get_hash_code(new_grp);
      ALOGE("group hash code is: %u\n", hash_code);
      index = hash_code % key_file->grps_hash_size;
      ALOGE("group index is: %u\n", index);
      grp = alloc_group();
      if(grp == NULL) {
         return FALSE;
      }
      grp_name_len = strlen(new_grp);
      grp->grp_name = (char *)malloc(
                                  sizeof(char) * (grp_name_len + 1));
      if(grp->grp_name == NULL) {
         ALOGE("could not alloc memory for group name\n");
         ALOGE("Add group failed\n");
         free_grp_list(grp);
         return FALSE;
      }else {
         memcpy(grp->grp_name, new_grp, (grp_name_len + 1));
      }
      grp->grp_next = key_file->grps_hash[index];
      key_file->grps_hash[index] = grp;
      key_file->num_of_grps++;
      return TRUE;
  }else {
     return FALSE;
  }
}

//checks validity of a group
//a valid group is
//inside [] group name must be
//alphanumeric
//Example: [grpName]
static char line_is_grp
(
  group_table *key_file,
  const char *str,
  char **cur_grp
)
{
  const char *g_start;
  const char *g_end;
  char *new_grp;
  unsigned int grp_len;

  if ((str == NULL) || (key_file == NULL)) {
      ALOGE("str is null or key file is null\n");
      return FALSE;
  }
  //checks start mark char ']'
  if(((*str) != '[')) {
      ALOGE("start mark is not '['\n");
      return FALSE;
  }else {
      str++;
      g_start = str;
  }
  //checks the end char '['
  while((*str != '\0') && ((*str) != ']')) {
        str++;
  }
  //if end mark group not found
  if ((*str) != ']') {
       ALOGE("grp end mark is not '['\n");
       return FALSE;
  }else {
       g_end = (str - 1);
  }

  str++;
  //if end mark found checks the rest chars as well
  //rest chars should be space
  while(((*str) == ' ') || ((*str) == '\t')) {
        str++;
  }
  if(*str) {
     ALOGE("after ']' there are some character\n");
     return FALSE;
  }

  str = g_start;
  while((*g_start != '\0') && (g_start != g_end)
         && isalnum(*g_start)) {
        g_start++;
  }
  if((g_start == g_end) && isalnum(*g_start)) {
      //look up if already exist
      //return false else insert the grp in grp table
      grp_len = (g_end - str + 1);
      new_grp = (char *)malloc(sizeof(char) * (grp_len + 1));
      if (new_grp == NULL) {
          ALOGE("could not alloc memory for new group\n");
          return FALSE;
      }
      memcpy(new_grp, str, grp_len);
      new_grp[grp_len] = '\0';

      if(add_grp(key_file, new_grp)) {
          free(*cur_grp);
         *cur_grp = new_grp;
         return TRUE;
      }else {
         ALOGE("could not add group to group table\n");
         return FALSE;
      }
  }else {
      return FALSE;
  }
}

//checks validity of key
//a valid key must start in
//a seperate line and key must
//be alphanumeric and before '='
//there must not be any space
//Example: key=value
static char line_is_key_value_pair
(
  group_table *key_file,
  const char *str,
  const char *cur_grp
)
{
  const char *equal_start;
  char *key = NULL;
  char *val = NULL;
  unsigned key_len;
  unsigned val_len;

  if((str == NULL) || (cur_grp == NULL) ||
     !strcmp(cur_grp, "") || (key_file == NULL)) {
     ALOGE("line is null or cur group or key file is null or empty\n");
     return FALSE;
  }
  equal_start = strchr(str, '=');
  key_len = (equal_start - str);
  if((equal_start == NULL) || (equal_start == str)) {
     ALOGE("line does not have '=' character or no key\n");
     return FALSE;
  }
  while((str != equal_start) && isalnum(*str))
        str++;
  if (str == equal_start) {
      key = (char *)malloc(sizeof(char) * (key_len + 1));
      if(key == NULL) {
         ALOGE("could not alloc memory for new key\n");
         return FALSE;
      }
      equal_start++;
      val_len = strlen(equal_start);
      val = (char *)malloc(sizeof(char) * (val_len + 1));
      if(val == NULL) {
         ALOGE("could not alloc memory for value\n");
         if(key){
             free(key);
             key = NULL;
         }
         return FALSE;
      }
      memcpy(key, (str - key_len), key_len);
      memcpy(val, equal_start, val_len);
      key[key_len] = '\0';
      val[val_len] = '\0';
      ALOGE("Grp: %s, key: %s, value: %s\n", cur_grp, key, val);
      return add_key_value_pair(key_file,
                                 cur_grp, key, val);
  }else {
     ALOGE("key name doesnot have alpha numeric char\n");
     return FALSE;
  }
}

static char add_key_value_pair
(
  group_table *key_file,
  const char *cur_grp,
  const char *key,
  const char *val
)
{
  unsigned int grp_hash_code;
  unsigned int key_hash_code;
  unsigned int grp_index;
  unsigned int key_index;
  unsigned key_len, val_len;
  group *grp = NULL;
  key_value_pair_list *list = NULL;

  if((key_file != NULL) && (cur_grp != NULL)
      && (key != NULL) && ((key_file->grps_hash != NULL))
      && (strcmp(key, ""))) {
     grp_hash_code = get_hash_code(cur_grp);
     ALOGE("grp hash code is %u\n", grp_hash_code);
     grp_index = (grp_hash_code % key_file->grps_hash_size);
     ALOGE("grp index is %u\n", grp_index);
     grp = key_file->grps_hash[grp_index];
     key_hash_code = get_hash_code(key);
     while((grp != NULL)) {
           if(!strcmp(cur_grp, grp->grp_name)) {
              key_index = (key_hash_code % grp->keys_hash_size);
              if(grp->list) {
                 list = grp->list[key_index];
              }else {
                 ALOGE("group list is null\n");
                 goto err;
              }
              while((list != NULL) && strcmp(key, list->key)) {
                    list = list->next;
              }
              if(list != NULL) {
                  ALOGE("group already contains the key\n");
                  goto err;
              }else{
                  list = alloc_key_value_pair();
                  if(list == NULL) {
                     ALOGE("add key value failed as could not alloc memory for key\
                            val pair\n");
                     goto err;
                  }
                  key_len = strlen(key);
                  list->key = (char *)malloc(sizeof(char) *
                                              (key_len + 1));
                  if(list->key == NULL) {
                     ALOGE("could not alloc memory for key\n");
                     free(list);
                     goto err;
                  }
                  val_len = strlen(val);
                  list->value = (char *)malloc(sizeof(char) *
                                                (val_len + 1));
                  if(!list->value) {
                      free(list->key);
                      free(list);
                      goto err;
                  }
                  memcpy(list->key, key, key_len);
                  memcpy(list->value, val, val_len);
                  if (key) free((char*)key);
                  if (val) free((char*)val);
                  list->key[key_len] = '\0';
                  list->value[val_len] = '\0';
                  list->next = grp->list[key_index];
                  grp->list[key_index] = list;
                  grp->num_of_keys++;
                  return TRUE;
              }
           }
           grp = grp->grp_next;
     }
     ALOGE("group does not exist\n");
     goto err;
  }
err:
     if (key) free((char*)key);
     if (val) free((char*)val);
     return FALSE;
}
