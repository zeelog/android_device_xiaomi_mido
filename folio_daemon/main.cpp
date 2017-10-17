/*
 * Copyright 2017 The Android Open Source Project
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
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <android/looper.h>
#include <android/sensor.h>
#include <cutils/log.h>

// Hall-effect sensor type
#define SENSOR_TYPE 33171016

#define RETRY_LIMIT     120
#define RETRY_PERIOD    30          // 30 seconds
#define WARN_PERIOD     (time_t)300 // 5 minutes

/*
 * This simple daemon listens for events from the Hall-effect sensor and writes
 * the appropriate SW_LID event to a uinput node. This allows the screen to be
 * locked with a magnetic folio case.
 */
int main(void) {
    int uinputFd;
    int err;
    struct uinput_user_dev uidev;
    ASensorManager *sensorManager = nullptr;
    ASensorRef hallSensor;
    ALooper *looper;
    ASensorEventQueue *eventQueue = nullptr;
    time_t lastWarn = 0;
    int attemptCount = 0;

    ALOGI("Started");

    uinputFd = TEMP_FAILURE_RETRY(open("/dev/uinput", O_WRONLY | O_NONBLOCK));
    if (uinputFd < 0) {
        ALOGE("Unable to open uinput node: %s", strerror(errno));
        goto out;
    }

    err = TEMP_FAILURE_RETRY(ioctl(uinputFd, UI_SET_EVBIT, EV_SW))
        | TEMP_FAILURE_RETRY(ioctl(uinputFd, UI_SET_EVBIT, EV_SYN))
        | TEMP_FAILURE_RETRY(ioctl(uinputFd, UI_SET_SWBIT, SW_LID));
    if (err != 0) {
        ALOGE("Unable to enable SW_LID events: %s", strerror(errno));
        goto out;
    }

    memset(&uidev, 0, sizeof (uidev));
    snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "uinput-folio");
    uidev.id.bustype = BUS_VIRTUAL;
    uidev.id.vendor = 0;
    uidev.id.product = 0;
    uidev.id.version = 0;

    err = TEMP_FAILURE_RETRY(write(uinputFd, &uidev, sizeof (uidev)));
    if (err < 0) {
        ALOGE("Write user device to uinput node failed: %s", strerror(errno));
        goto out;
    }

    err = TEMP_FAILURE_RETRY(ioctl(uinputFd, UI_DEV_CREATE));
    if (err < 0) {
        ALOGE("Unable to create uinput device: %s", strerror(errno));
        goto out;
    }

    ALOGI("Successfully registered uinput-folio for SW_LID events");

    // Get Hall-effect sensor events from the NDK
    sensorManager = ASensorManager_getInstanceForPackage(nullptr);
    /*
     * As long as we are unable to get the sensor handle, periodically retry
     * and emit an error message at a low frequency to prevent high CPU usage
     * and log spam. If we simply exited with an error here, we would be
     * immediately restarted and fail in the same way indefinitely.
     */
    while (true) {
        time_t now = time(NULL);
        hallSensor = ASensorManager_getDefaultSensor(sensorManager,
                                                     SENSOR_TYPE);
        if (hallSensor != nullptr) {
            break;
        }

        if (++attemptCount >= RETRY_LIMIT) {
            ALOGE("Retries exhausted; exiting");
            goto out;
        } else if (now > lastWarn + WARN_PERIOD) {
            ALOGE("Unable to get Hall-effect sensor");
            lastWarn = now;
        }

        sleep(RETRY_PERIOD);
    }

    looper = ALooper_forThread();
    if (looper == nullptr) {
        looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    }

    eventQueue = ASensorManager_createEventQueue(sensorManager, looper, 0, NULL,
                                                 NULL);
    err = ASensorEventQueue_registerSensor(eventQueue, hallSensor,
                                           ASensor_getMinDelay(hallSensor),
                                           10000);
    if (err < 0) {
        ALOGE("Unable to register for Hall-effect sensor events");
        goto out;
    }

    ALOGI("Starting polling loop");

    // Polling loop
    while (ALooper_pollAll(-1, NULL, NULL, NULL) == 0) {
        int eventCount = 0;
        ASensorEvent sensorEvent;
        while (ASensorEventQueue_getEvents(eventQueue, &sensorEvent, 1) > 0) {
            // 1 means closed; 0 means open
            int isClosed = sensorEvent.data[0] > 0.0f ? 1 : 0;
            struct input_event event;
            event.type = EV_SW;
            event.code = SW_LID;
            event.value = isClosed;
            err = TEMP_FAILURE_RETRY(write(uinputFd, &event, sizeof (event)));
            if (err < 0) {
                ALOGE("Write EV_SW to uinput node failed: %s", strerror(errno));
                goto out;
            }

            // Force a flush with an EV_SYN
            event.type = EV_SYN;
            event.code = SYN_REPORT;
            event.value = 0;
            err = TEMP_FAILURE_RETRY(write(uinputFd, &event, sizeof (event)));
            if (err < 0) {
                ALOGE("Write EV_SYN to uinput node failed: %s",
                      strerror(errno));
                goto out;
            }

            ALOGI("Sent lid %s event", isClosed ? "closed" : "open");
            eventCount++;
        }

        if (eventCount == 0) {
            ALOGE("Poll returned with zero events: %s", strerror(errno));
            break;
        }
    }

out:
    // Clean up
    if (sensorManager != nullptr && eventQueue != nullptr) {
        ASensorManager_destroyEventQueue(sensorManager, eventQueue);
    }

    if (uinputFd >= 0) {
        close(uinputFd);
    }

    // The loop can only be exited via failure or signal
    return 1;
}
