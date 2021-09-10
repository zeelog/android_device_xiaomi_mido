/*
* Copyright (c) 2016-2020, The Linux Foundation. All rights reserved.
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

#define LOG_TAG "audio_hw_sndmonitor"
/*#define LOG_NDEBUG 0*/
#define LOG_NDDEBUG 0

/* monitor sound card, cpe state

   audio_dev registers for a callback from this module in adev_open
   Each stream in audio_hal registers for a callback in
   adev_open_*_stream.

   A thread is spawned to poll() on sound card state files in /proc.
   On observing a sound card state change, this thread invokes the
   callbacks registered.

   Callbacks are deregistered in adev_close_*_stream and adev_close
*/
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <cutils/list.h>
#include <cutils/hashmap.h>
#include <log/log.h>
#include <cutils/str_parms.h>
#include <ctype.h>

#include "audio_hw.h"
#include "audio_extn.h"

#ifdef DYNAMIC_LOG_ENABLED
#include <log_xml_parser.h>
#define LOG_MASK HAL_MOD_FILE_SND_MONITOR
#include <log_utils.h>
#endif

//#define MONITOR_DEVICE_EVENTS
#define CPE_MAGIC_NUM 0x2000
#define MAX_CPE_SLEEP_RETRY 2
#define CPE_SLEEP_WAIT 100

#define SPLI_STATE_PATH "/proc/wcd-spi-ac/svc-state"
#define SLPI_MAGIC_NUM 0x3000
#define MAX_SLPI_SLEEP_RETRY 2
#define SLPI_SLEEP_WAIT_MS 100

#define MAX_SLEEP_RETRY 100
#define AUDIO_INIT_SLEEP_WAIT 100 /* 100 ms */

#define AUDIO_PARAMETER_KEY_EXT_AUDIO_DEVICE "ext_audio_device"
#define INIT_MAP_SIZE 5

typedef enum {
    audio_event_on,
    audio_event_off
} audio_event_status;

typedef struct {
    int card;
    int fd;
    struct listnode node; // membership in sndcards list
    card_status_t status;
} sndcard_t;

typedef struct {
    char *dev;
    int fd;
    int status;
    struct listnode node; // membership in deviceevents list;
} dev_event_t;

typedef void (*notifyfn)(const void *target, const char *msg);

typedef struct {
    const void *target;
    notifyfn notify;
    struct listnode cards;
    unsigned int num_cards;
    struct listnode dev_events;
    unsigned int num_dev_events;
    pthread_t monitor_thread;
    int intpipe[2];
    Hashmap *listeners; // from stream * -> callback func
    bool initcheck;
} sndmonitor_state_t;

static sndmonitor_state_t sndmonitor;

static char *read_state(int fd)
{
    struct stat buf;
    if (fstat(fd, &buf) < 0)
        return NULL;

    off_t pos = lseek(fd, 0, SEEK_CUR);
    off_t avail = buf.st_size - pos;
    if (avail <= 0) {
        ALOGE("avail %ld", avail);
        return NULL;
    }

    char *state = (char *)calloc(avail+1, sizeof(char));
    if (!state)
        return NULL;

    ssize_t bytes = read(fd, state, avail);
    if (bytes <= 0)
        return NULL;

    // trim trailing whitespace
    while (bytes && isspace(*(state+bytes-1))) {
        *(state + bytes - 1) = '\0';
        --bytes;
    }
    lseek(fd, 0, SEEK_SET);
    return state;
}

static int add_new_sndcard(int card, int fd)
{
    sndcard_t *s = (sndcard_t *)calloc(sizeof(sndcard_t), 1);

    if (!s)
        return -1;

    s->card = card;
    s->fd = fd; // dup?

    char *state = read_state(fd);

    if (!state) {
        free(s);
        return -1;
    }
    bool online = state && !strcmp(state, "ONLINE");

    ALOGV("card %d initial state %s %d", card, state, online);

    if (state)
        free(state);

    s->status = online ? CARD_STATUS_ONLINE : CARD_STATUS_OFFLINE;
    list_add_tail(&sndmonitor.cards, &s->node);
    return 0;
}

static int enum_sndcards()
{
    const char *cards = "/proc/asound/cards";
    int tries = 10;
    char *line = NULL;
    size_t len = 0;
    ssize_t bytes_read = -1;
    char path[128] = {0};
    char *ptr = NULL, *saveptr = NULL, *card_id = NULL;
    int line_no=0;
    unsigned int num_cards=0, num_cpe=0;
    FILE *fp = NULL;
    int fd = -1, ret = -1;

    while (--tries) {
        if ((fp = fopen(cards, "r")) == NULL) {
            ALOGE("Cannot open %s file to get list of sound cards", cards);
            usleep(100000);
            continue;
        }
        break;
    }

    if (!tries)
        return -ENODEV;

    while ((bytes_read = getline(&line, &len, fp) != -1)) {
        // skip every other line to to match
        // the output format of /proc/asound/cards
        if (line_no++ % 2)
            continue;

        ptr = strtok_r(line, " [", &saveptr);
        if (!ptr)
            continue;

        card_id = strtok_r(saveptr+1, "]", &saveptr);
        if (!card_id)
            continue;

        // Only consider sound cards associated with ADSP
        if ((strncasecmp(card_id, "msm", 3) != 0) &&
            (strncasecmp(card_id, "sdm", 3) != 0) &&
            (strncasecmp(card_id, "sdc", 3) != 0) &&
            (strncasecmp(card_id, "sm", 2) != 0) &&
            (strncasecmp(card_id, "trinket", 7) != 0) &&
            (strncasecmp(card_id, "apq", 3) != 0) &&
            (strncasecmp(card_id, "sa", 2) != 0) &&
            (strncasecmp(card_id, "kona", 4) != 0) &&
            (strncasecmp(card_id, "holi", 4) != 0) &&
            (strncasecmp(card_id, "shima", 5) != 0) &&
            (strncasecmp(card_id, "lahaina", 7) != 0) &&
            (strncasecmp(card_id, "atoll", 5) != 0) &&
            (strncasecmp(card_id, "bengal", 6) != 0) &&
            (strncasecmp(card_id, "lito", 4) != 0)) {
            ALOGW("Skip over non-ADSP snd card %s", card_id);
            continue;
        }

        snprintf(path, sizeof(path), "/proc/asound/card%s/state", ptr);
        ALOGV("Opening sound card state : %s", path);

        fd = open(path, O_RDONLY);
        if (fd == -1) {
            ALOGE("Open %s failed : %s", path, strerror(errno));
            continue;
        }

        ret = add_new_sndcard(atoi(ptr), fd);
        if (ret != 0) {
            close(fd);
            continue;
        }

        num_cards++;

        // query cpe state for this card as well
        tries = MAX_CPE_SLEEP_RETRY;
        snprintf(path, sizeof(path), "/proc/asound/card%s/cpe0_state", ptr);

        if (access(path, R_OK) < 0) {
            ALOGW("access %s failed w/ err %s", path, strerror(errno));
            continue;
        }

        ALOGV("Open cpe state card state %s", path);
        while (--tries) {
            if ((fd = open(path, O_RDONLY)) < 0) {
                ALOGW("Open cpe state card state failed, retry : %s", path);
                usleep(CPE_SLEEP_WAIT*1000);
                continue;
            }
            break;
        }

        if (!tries)
            continue;

        ret = add_new_sndcard(CPE_MAGIC_NUM+num_cpe, fd);
        if (ret != 0) {
            close(fd);
            continue;
        }

        num_cpe++;
        num_cards++;
    }
    if (line)
        free(line);
    fclose(fp);

    /* Add fd to query for SLPI status */
    if (access(SPLI_STATE_PATH, R_OK) < 0) {
        ALOGV("access to %s failed: %s", SPLI_STATE_PATH, strerror(errno));
    } else {
        tries = MAX_SLPI_SLEEP_RETRY;
        ALOGV("Open %s", SPLI_STATE_PATH);
        while (tries--) {
            if ((fd = open(SPLI_STATE_PATH, O_RDONLY)) < 0) {
                ALOGW("Open %s failed %s, retry", SPLI_STATE_PATH,
                      strerror(errno));
                usleep(SLPI_SLEEP_WAIT_MS * 1000);
                continue;
            }
            break;
        }
        if (fd >= 0) {
            ret = add_new_sndcard(SLPI_MAGIC_NUM, fd);
            if (ret != 0)
                close(fd);
            else
                num_cards++;
        }
    }

    ALOGV("sndmonitor registerer num_cards %d", num_cards);
    sndmonitor.num_cards = num_cards;
    return num_cards ? 0 : -1;
}

static void free_sndcards()
{
    while (!list_empty(&sndmonitor.cards)) {
        struct listnode *n = list_head(&sndmonitor.cards);
        sndcard_t *s = node_to_item(n, sndcard_t, node);
        list_remove(n);
        close(s->fd);
        free(s);
    }
}

#ifdef MONITOR_DEVICE_EVENTS
static int add_new_dev_event(char *d_name, int fd)
{
    dev_event_t *d = (dev_event_t *)calloc(sizeof(dev_event_t), 1);

    if (!d)
        return -1;

    d->dev = strdup(d_name);
    d->fd = fd;
    list_add_tail(&sndmonitor.dev_events, &d->node);
    return 0;
}

static int enum_dev_events()
{
    const char *events_dir = "/sys/class/switch/";
    DIR *dp;
    struct dirent *in_file;
    int fd;
    char path[128] = {0};
    unsigned int num_dev_events = 0;

    if ((dp = opendir(events_dir)) == NULL) {
        ALOGE("Cannot open switch directory %s err %s",
              events_dir, strerror(errno));
        return -1;
    }

    while ((in_file = readdir(dp)) != NULL) {
        if (!strstr(in_file->d_name, "qc_"))
            continue;

        snprintf(path, sizeof(path), "%s/%s/state",
                 events_dir, in_file->d_name);

        ALOGV("Opening audio dev event state : %s ", path);
        fd = open(path, O_RDONLY);
        if (fd == -1) {
            ALOGE("Open %s failed : %s", path, strerror(errno));
        } else {
            if (!add_new_dev_event(in_file->d_name, fd))
                num_dev_events++;
        }
    }
    closedir(dp);
    sndmonitor.num_dev_events = num_dev_events;
    return num_dev_events ? 0 : -1;
}
#endif

static void free_dev_events()
{
    while (!list_empty(&sndmonitor.dev_events)) {
        struct listnode *n = list_head(&sndmonitor.dev_events);
        dev_event_t *d = node_to_item(n, dev_event_t, node);
        list_remove(n);
        close(d->fd);
        free(d->dev);
        free(d);
    }
}

static int notify(const struct str_parms *params)
{
    if (!params)
        return -1;

    char *str = str_parms_to_str((struct str_parms *)params);

    if (!str)
        return -1;

    if (sndmonitor.notify)
        sndmonitor.notify(sndmonitor.target, str);

    ALOGV("%s", str);
    free(str);
    return 0;
}

int on_dev_event(dev_event_t *dev_event)
{
    char state_buf[2];
    if (read(dev_event->fd, state_buf, 1) <= 0)
        return -1;

    lseek(dev_event->fd, 0, SEEK_SET);
    state_buf[1]='\0';
    if (atoi(state_buf) == dev_event->status)
        return 0;

    dev_event->status = atoi(state_buf);

    struct str_parms *params = str_parms_create();

    if (!params)
        return -1;

    char val[32] = {0};
    snprintf(val, sizeof(val), "%s,%s", dev_event->dev,
             dev_event->status ? "ON" : "OFF");

    if (str_parms_add_str(params, AUDIO_PARAMETER_KEY_EXT_AUDIO_DEVICE, val) < 0)
        return -1;

    int ret = notify(params);
    str_parms_destroy(params);
    return ret;
}

bool on_sndcard_state_update(sndcard_t *s)
{
    char rd_buf[9]={0};
    card_status_t status;

    if (read(s->fd, rd_buf, 8) <= 0)
        return -1;

    rd_buf[8] = '\0';
    lseek(s->fd, 0, SEEK_SET);

    ALOGV("card num %d, new state %s", s->card, rd_buf);

    if (strstr(rd_buf, "OFFLINE"))
        status = CARD_STATUS_OFFLINE;
    else if (strstr(rd_buf, "ONLINE"))
        status = CARD_STATUS_ONLINE;
    else {
        ALOGE("unknown state");
        return 0;
    }

    if (status == s->status) // no change
        return 0;

    s->status = status;

    struct str_parms *params = str_parms_create();

    if (!params)
        return -1;

    char val[32] = {0};
    bool is_cpe = ((s->card >= CPE_MAGIC_NUM) && (s->card < SLPI_MAGIC_NUM));
    bool is_slpi = (s->card == SLPI_MAGIC_NUM);
    char *key = NULL;
    /*
     * cpe actual card num is (card - CPE_MAGIC_NUM), so subtract accordingly.
     * SLPI actual fd num is (card - SLPI_MAGIC_NUM), so subtract accordingly.
     */
    snprintf(val, sizeof(val), "%d,%s",
        s->card - (is_cpe ? CPE_MAGIC_NUM : (is_slpi ? SLPI_MAGIC_NUM : 0)),
                status == CARD_STATUS_ONLINE ? "ONLINE" : "OFFLINE");
    key = (is_cpe ?  "CPE_STATUS" :
          (is_slpi ? "SLPI_STATUS" :
                     "SND_CARD_STATUS"));
    if (str_parms_add_str(params, key, val) < 0)
        return -1;

    int ret = notify(params);
    str_parms_destroy(params);
    return ret;
}

void *monitor_thread_loop(void *args __unused)
{
    ALOGV("Start threadLoop()");
    unsigned int num_poll_fds = sndmonitor.num_cards +
                                sndmonitor.num_dev_events + 1/*pipe*/;
    struct pollfd *pfd = (struct pollfd *)calloc(sizeof(struct pollfd),
                                                  num_poll_fds);
    if (!pfd)
        return NULL;

    pfd[0].fd = sndmonitor.intpipe[0];
    pfd[0].events = POLLPRI|POLLIN;

    int i = 1;
    struct listnode *node;
    list_for_each(node, &sndmonitor.cards) {
        sndcard_t *s = node_to_item(node, sndcard_t, node);
        pfd[i].fd = s->fd;
        pfd[i].events = POLLPRI;
        ++i;
    }

    list_for_each(node, &sndmonitor.dev_events) {
        dev_event_t *d = node_to_item(node, dev_event_t, node);
        pfd[i].fd = d->fd;
        pfd[i].events = POLLPRI;
        ++i;
    }

    while (1) {
        if (poll(pfd, num_poll_fds, -1) < 0) {
            int errno_ = errno;
            ALOGE("poll() failed w/ err %s", strerror(errno_));
            switch (errno_) {
                case EINTR:
                case ENOMEM:
                    sleep(2);
                    continue;
                default:
                    /* above errors can be caused due to current system
                       state .. any other error is not expected */
                    LOG_ALWAYS_FATAL("unxpected poll() system call failure");
                    break;
            }
        }
        ALOGV("out of poll()");

#define READY_TO_READ(p) ((p)->revents & (POLLIN|POLLPRI))
#define ERROR_IN_FD(p) ((p)->revents & (POLLERR|POLLHUP|POLLNVAL))

        // check if requested to exit
        if (READY_TO_READ(&pfd[0])) {
            char buf[2]={0};
            read(pfd[0].fd, buf, 1);
            if (!strcmp(buf, "Q"))
                break;
        } else if (ERROR_IN_FD(&pfd[0])) {
            // do not consider for poll again
            // POLLERR - can this happen?
            // POLLHUP - adev must not close pipe
            // POLLNVAL - fd is valid
            LOG_ALWAYS_FATAL("unxpected error in pipe poll fd 0x%x",
                             pfd[0].revents);
            // FIXME: If not fatal, then need some logic to close
            // these fds on error
            pfd[0].fd *= -1;
        }

        i = 1;
        list_for_each(node, &sndmonitor.cards) {
            sndcard_t *s = node_to_item(node, sndcard_t, node);
            if (READY_TO_READ(&pfd[i]))
                on_sndcard_state_update(s);
            else if (ERROR_IN_FD(&pfd[i])) {
                // do not consider for poll again
                // POLLERR - can this happen as we are reading from a fs?
                // POLLHUP - not valid for cardN/state
                // POLLNVAL - fd is valid
                LOG_ALWAYS_FATAL("unxpected error in card poll fd 0x%x",
                                 pfd[i].revents);
                // FIXME: If not fatal, then need some logic to close
                // these fds on error
                pfd[i].fd *= -1;
            }
            ++i;
        }

        list_for_each(node, &sndmonitor.dev_events) {
            dev_event_t *d = node_to_item(node, dev_event_t, node);
            if (READY_TO_READ(&pfd[i]))
                on_dev_event(d);
            else if (ERROR_IN_FD(&pfd[i])) {
                // do not consider for poll again
                // POLLERR - can this happen as we are reading from a fs?
                // POLLHUP - not valid for switch/state
                // POLLNVAL - fd is valid
                LOG_ALWAYS_FATAL("unxpected error in dev poll fd 0x%x",
                                 pfd[i].revents);
                // FIXME: If not fatal, then need some logic to close
                // these fds on error
                pfd[i].fd *= -1;
            }
            ++i;
        }
    }
    if (pfd)
        free(pfd);
    return NULL;
}

// ---- listener static APIs ---- //
static int hashfn(void *key)
{
    return (int)key;
}

static bool hasheq(void *key1, void *key2)
{
    return key1 == key2;
}

static bool snd_cb(void *key, void *value, void *context)
{
    snd_mon_cb cb = (snd_mon_cb)value;
    cb(key, context);
    return true;
}

static void snd_mon_update(const void *target __unused, const char *msg)
{
    // target can be used to check if this message is intended for the
    // recipient or not. (using some statically saved state)

    struct str_parms *parms = str_parms_create_str(msg);

    if (!parms)
        return;

    hashmapLock(sndmonitor.listeners);
    hashmapForEach(sndmonitor.listeners, snd_cb, parms);
    hashmapUnlock(sndmonitor.listeners);

    str_parms_destroy(parms);
}

static int listeners_init()
{
    sndmonitor.listeners = hashmapCreate(INIT_MAP_SIZE, hashfn, hasheq);
    if (!sndmonitor.listeners)
        return -1;
    return 0;
}

static int listeners_deinit()
{
    // XXX TBD
    return -1;
}

static int add_listener(void *stream, snd_mon_cb cb)
{
    Hashmap *map = sndmonitor.listeners;
    hashmapLock(map);
    hashmapPut(map, stream, cb);
    hashmapUnlock(map);
    return 0;
}

static int del_listener(void * stream)
{
    Hashmap *map = sndmonitor.listeners;
    hashmapLock(map);
    hashmapRemove(map, stream);
    hashmapUnlock(map);
    return 0;
}

// --- public APIs --- //

int snd_mon_deinit()
{
    if (!sndmonitor.initcheck)
        return -1;

    write(sndmonitor.intpipe[1], "Q", 1);
    pthread_join(sndmonitor.monitor_thread, (void **) NULL);
    free_dev_events();
    listeners_deinit();
    free_sndcards();
    close(sndmonitor.intpipe[0]);
    close(sndmonitor.intpipe[1]);
    sndmonitor.initcheck = 0;
    return 0;
}

int snd_mon_init()
{
    sndmonitor.notify = snd_mon_update;
    sndmonitor.target = NULL; // unused for now
    list_init(&sndmonitor.cards);
    list_init(&sndmonitor.dev_events);
    sndmonitor.initcheck = false;

    if (pipe(sndmonitor.intpipe) < 0)
        goto pipe_error;

    if (enum_sndcards() < 0)
        goto enum_sncards_error;

    if (listeners_init() < 0)
        goto listeners_error;

#ifdef MONITOR_DEVICE_EVENTS
    enum_dev_events(); // failure here isn't fatal
#endif

    int ret = pthread_create(&sndmonitor.monitor_thread,
                             (const pthread_attr_t *) NULL,
                             monitor_thread_loop, NULL);

    if (ret) {
        goto monitor_thread_create_error;
    }
    sndmonitor.initcheck = true;
    return 0;

monitor_thread_create_error:
    listeners_deinit();
listeners_error:
    free_sndcards();
enum_sncards_error:
    close(sndmonitor.intpipe[0]);
    close(sndmonitor.intpipe[1]);
pipe_error:
    return -ENODEV;
}

int snd_mon_register_listener(void *stream, snd_mon_cb cb)
{
    if (!sndmonitor.initcheck) {
        ALOGW("sndmonitor initcheck failed, cannot register");
        return -1;
    }

    return add_listener(stream, cb);
}

int snd_mon_unregister_listener(void *stream)
{
    if (!sndmonitor.initcheck) {
        ALOGW("sndmonitor initcheck failed, cannot deregister");
        return -1;
    }

    ALOGV("deregister listener for stream %p ", stream);
    return del_listener(stream);
}
