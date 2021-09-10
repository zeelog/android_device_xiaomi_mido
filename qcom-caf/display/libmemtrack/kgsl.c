/*
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include <hardware/memtrack.h>

#include "memtrack_msm.h"

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))
#define min(x, y) ((x) < (y) ? (x) : (y))

struct memtrack_record record_templates[] = {
    {
        .flags = MEMTRACK_FLAG_SMAPS_ACCOUNTED |
                 MEMTRACK_FLAG_PRIVATE |
                 MEMTRACK_FLAG_NONSECURE,
    },
    {
        .flags = MEMTRACK_FLAG_SMAPS_UNACCOUNTED |
                 MEMTRACK_FLAG_PRIVATE |
                 MEMTRACK_FLAG_NONSECURE,
    },
};

int kgsl_memtrack_get_memory(pid_t pid, enum memtrack_type type,
                             struct memtrack_record *records,
                             size_t *num_records)
{
    size_t allocated_records = min(*num_records, ARRAY_SIZE(record_templates));
    char syspath[128];
    size_t accounted_size = 0;
    size_t unaccounted_size = 0;
    FILE *fp;
    int ret;

    *num_records = ARRAY_SIZE(record_templates);

    /* fastpath to return the necessary number of records */
    if (allocated_records == 0)
        return 0;

    memcpy(records, record_templates,
            sizeof(struct memtrack_record) * allocated_records);

    if (type == MEMTRACK_TYPE_GL) {

        snprintf(syspath, sizeof(syspath),
                 "/sys/class/kgsl/kgsl/proc/%d/gpumem_mapped", pid);

        fp = fopen(syspath, "r");
        if (fp == NULL)
            return -errno;

        ret = fscanf(fp, "%zu", &accounted_size);
        if (ret != 1) {
            fclose(fp);
            return -EINVAL;
        }
        fclose(fp);

        snprintf(syspath, sizeof(syspath),
                 "/sys/class/kgsl/kgsl/proc/%d/gpumem_unmapped", pid);

        fp = fopen(syspath, "r");
        if (fp == NULL) {
            fclose(fp);
            return -errno;
        }

        ret = fscanf(fp, "%zu", &unaccounted_size);
        if (ret != 1) {
            fclose(fp);
            return -EINVAL;
        }
        fclose(fp);

    } else if (type == MEMTRACK_TYPE_GRAPHICS) {

        snprintf(syspath, sizeof(syspath),
                 "/sys/class/kgsl/kgsl/proc/%d/imported_mem", pid);

        fp = fopen(syspath, "r");
        if (fp == NULL)
            return -errno;

        ret = fscanf(fp, "%zu", &unaccounted_size);
        if (ret != 1) {
            fclose(fp);
            return -EINVAL;
        }
        fclose(fp);
    }

    if (allocated_records > 0)
    records[0].size_in_bytes = accounted_size;

    if (allocated_records > 1)
    records[1].size_in_bytes = unaccounted_size;

    return 0;
}
