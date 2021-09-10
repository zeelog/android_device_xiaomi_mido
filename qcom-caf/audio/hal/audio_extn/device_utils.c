/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of The Linux Foundation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
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

#define LOG_TAG "device_utils"
/*#define LOG_NDEBUG 0*/
#define LOG_NDDEBUG 0

#include <errno.h>
#include <stdlib.h>
#include <hardware/audio.h>
#include <cutils/list.h>
#include <log/log.h>

#include "device_utils.h"

/*
 * Below are the devices for which is back end is same, SLIMBUS_0_RX.
 * All these devices are handled by the internal HW codec. We can
 * enable any one of these devices at any time. An exception here is
 * 44.1k headphone which uses different backend. This is filtered
 * as different hal internal device in the code but remains same
 * as standard android device AUDIO_DEVICE_OUT_WIRED_HEADPHONE
 * for other layers.
 */
static const uint32_t AUDIO_DEVICE_OUT_ALL_CODEC_BACKEND_ARRAY[] = {
    AUDIO_DEVICE_OUT_EARPIECE,
    AUDIO_DEVICE_OUT_SPEAKER,
    AUDIO_DEVICE_OUT_WIRED_HEADSET,
    AUDIO_DEVICE_OUT_WIRED_HEADPHONE,
    AUDIO_DEVICE_OUT_LINE,
    AUDIO_DEVICE_OUT_SPEAKER_SAFE,
};

/*
 * Below are the input devices for which back end is same, SLIMBUS_0_TX.
 * All these devices are handled by the internal HW codec. We can
 * enable any one of these devices at any time
 */
static const uint32_t AUDIO_DEVICE_IN_ALL_CODEC_BACKEND_ARRAY[] = {
    AUDIO_DEVICE_IN_BUILTIN_MIC,
    AUDIO_DEVICE_IN_WIRED_HEADSET,
    AUDIO_DEVICE_IN_VOICE_CALL,
    AUDIO_DEVICE_IN_BACK_MIC,
};

static const uint32_t AUDIO_DEVICE_OUT_CODEC_BACKEND_CNT =
                    AUDIO_ARRAY_SIZE(AUDIO_DEVICE_OUT_ALL_CODEC_BACKEND_ARRAY);
static const uint32_t AUDIO_DEVICE_IN_CODEC_BACKEND_CNT =
                    AUDIO_ARRAY_SIZE(AUDIO_DEVICE_IN_ALL_CODEC_BACKEND_ARRAY);


int list_length(struct listnode *list)
{
    struct listnode *node;
    int length = 0;

    if (list == NULL)
        goto done;

    for (node = list->next; node != list; node = node->next)
        ++length;
done:
    return length;
}

/*
 * Returns true if devices list contains input device type.
 */
bool is_audio_in_device_type(struct listnode *devices)
{
    struct listnode *node;
    struct audio_device_info *item = NULL;

    if (devices == NULL)
        return false;

    list_for_each (node, devices) {
        item = node_to_item(node, struct audio_device_info, list);
        if (item != NULL && audio_is_input_device(item->type)) {
            ALOGV("%s: in device %#x", __func__, item->type);
            return true;
        }
    }
    return false;
}

/*
 * Returns true if devices list contains output device type.
 */
bool is_audio_out_device_type(struct listnode *devices)
{
    struct listnode *node;
    struct audio_device_info *item = NULL;

    if (devices == NULL)
        return false;

    list_for_each (node, devices) {
        item = node_to_item(node, struct audio_device_info, list);
        if (item != NULL && audio_is_output_device(item->type)) {
            ALOGV("%s: out device %#x", __func__, item->type);
            return true;
        }
    }
    return false;
}

/*
 * Returns true if devices list contains codec backend
 * input device type.
 */
bool is_codec_backend_in_device_type(struct listnode *devices)
{
    struct listnode *node;
    struct audio_device_info *item = NULL;

    if (devices == NULL)
        return false;

    list_for_each (node, devices) {
        item = node_to_item(node, struct audio_device_info, list);
        if (item == NULL)
            return false;
        for (int i = 0; i < AUDIO_DEVICE_IN_CODEC_BACKEND_CNT; i++) {
            if (item->type == AUDIO_DEVICE_IN_ALL_CODEC_BACKEND_ARRAY[i]) {
                ALOGV("%s: codec backend in device %#x", __func__, item->type);
                return true;
            }
        }
    }
    return false;
}

/*
 * Returns true if devices list contains codec backend
 * output device type.
 */
bool is_codec_backend_out_device_type(struct listnode *devices)
{
    struct listnode *node;
    struct audio_device_info *item = NULL;

    if (devices == NULL)
        return false;

    list_for_each (node, devices) {
        item = node_to_item(node, struct audio_device_info, list);
        if (item == NULL)
            return false;
        for (int i = 0; i < AUDIO_DEVICE_OUT_CODEC_BACKEND_CNT; i++) {
            if (item->type == AUDIO_DEVICE_OUT_ALL_CODEC_BACKEND_ARRAY[i]) {
                ALOGV("%s: codec backend out device %#x", __func__, item->type);
                return true;
            }
        }
    }
    return false;
}

/* Returns true if USB input device is found in passed devices list */
bool is_usb_in_device_type(struct listnode *devices)
{
    struct listnode *node;
    struct audio_device_info *item = NULL;

    if (devices == NULL)
        return false;

    list_for_each (node, devices) {
        item = node_to_item(node, struct audio_device_info, list);
        if (item != NULL && audio_is_usb_in_device(item->type)) {
            ALOGV("%s: USB in device %#x", __func__, item->type);
            return true;
        }
    }
    return false;
}

/*
 * Returns true if USB output device is found in passed devices list
 */
bool is_usb_out_device_type(struct listnode *devices)
{
    struct listnode *node;
    struct audio_device_info *item = NULL;

    list_for_each (node, devices) {
        item = node_to_item(node, struct audio_device_info, list);
        if (item != NULL && audio_is_usb_out_device(item->type)) {
            ALOGV("%s: USB out device %#x address %s", __func__,
                   item->type, item->address);
            return true;
        }
    }
    return false;
}

/*
 * Returns USB device address information (card, device)
 */
const char *get_usb_device_address(struct listnode *devices)
{
    struct listnode *node;
    struct audio_device_info *item = NULL;

    if (devices == NULL)
        return "";

    list_for_each (node, devices) {
        item = node_to_item(node, struct audio_device_info, list);
        if (item != NULL && (audio_is_usb_out_device(item->type) ||
                        audio_is_usb_in_device(item->type))) {
            ALOGV("%s: USB device %#x address %s", __func__,
                   item->type, item->address);
            return (const char *)&item->address[0];
        }
    }
    return "";
}

/*
 * Returns true if SCO output device is found in passed devices list
 */
bool is_sco_in_device_type(struct listnode *devices)
{
    struct listnode *node;
    struct audio_device_info *item = NULL;

    if (devices == NULL)
        return false;

    list_for_each (node, devices) {
        item = node_to_item(node, struct audio_device_info, list);
        if (item != NULL &&
                audio_is_bluetooth_in_sco_device(item->type)) {
            ALOGV("%s: SCO in device %#x", __func__, item->type);
            return true;
        }
    }
    return false;
}

/*
 * Returns true if SCO output device is found in passed devices list
 */
bool is_sco_out_device_type(struct listnode *devices)
{
    struct listnode *node;
    struct audio_device_info *item = NULL;

    if (devices == NULL)
        return false;

    list_for_each (node, devices) {
        item = node_to_item(node, struct audio_device_info, list);
        if (item != NULL &&
                audio_is_bluetooth_out_sco_device(item->type)) {
            ALOGV("%s: SCO out device %#x", __func__, item->type);
            return true;
        }
    }
    return false;
}

/*
 * Returns true if A2DP input device is found in passed devices list
 */
bool is_a2dp_in_device_type(struct listnode *devices)
{
    struct listnode *node;
    struct audio_device_info *item = NULL;

    if (devices == NULL)
        return false;

    list_for_each (node, devices) {
        item = node_to_item(node, struct audio_device_info, list);
        if (item != NULL && audio_is_a2dp_in_device(item->type)) {
            ALOGV("%s: A2DP in device %#x", __func__, item->type);
            return true;
        }
    }
    return false;
}

/*
 * Returns true if A2DP output device is found in passed devices list
 */
bool is_a2dp_out_device_type(struct listnode *devices)
{
    struct listnode *node;
    struct audio_device_info *item = NULL;

    if (devices == NULL)
        return false;

    list_for_each (node, devices) {
        item = node_to_item(node, struct audio_device_info, list);
        if (item != NULL && audio_is_a2dp_out_device(item->type)) {
            ALOGV("%s: A2DP out device %#x", __func__, item->type);
            return true;
        }
    }
    return false;
}

/*
 * Clear device list
 * Operation: devices = {};
 */

int clear_devices(struct listnode *devices)
{
    struct listnode *node, *temp;
    struct audio_device_info *item = NULL;

    if (devices == NULL)
        return 0;

    list_for_each_safe (node, temp, devices) {
        item = node_to_item(node, struct audio_device_info, list);
        if (item != NULL) {
            list_remove(&item->list);
            free(item);
        }
    }

    return 0;
}

/*
 * Check if a device with given type is present in devices list
 */
bool compare_device_type(struct listnode *devices, audio_devices_t device_type)
{
    struct listnode *node;
    struct audio_device_info *item = NULL;

    if (devices == NULL)
        return false;

    list_for_each (node, devices) {
        item = node_to_item(node, struct audio_device_info, list);
        if (item != NULL && (item->type == device_type)) {
            ALOGV("%s: device types %d match", __func__, device_type);
            return true;
        }
    }
    return false;
}

/*
 * Check if a device with given type and address is present in devices list
 */
bool compare_device_type_and_address(struct listnode *devices,
                                     audio_devices_t type, const char* address)
{
    struct listnode *node = devices;
    struct audio_device_info *item = NULL;

    if (devices == NULL)
        return false;

    list_for_each (node, devices) {
        item = node_to_item(node, struct audio_device_info, list);
        if (item != NULL && (item->type == type) &&
            (strcmp((const char *)&item->address[0], address) == 0)) {
            ALOGV("%s: device type %x and address %s match", __func__,
                item->type, (const char *)&item->address[0]);
            return true;
        }
    }
    return false;
}

/*
 * Returns true if intersection of d1 and d2 is not NULL
 */
bool compare_devices_for_any_match(struct listnode *d1, struct listnode *d2)
{
    struct listnode *node;
    struct audio_device_info *item = NULL;

    if (d1 == NULL || d2 == NULL)
        return false;

    list_for_each (node, d1) {
        item = node_to_item(node, struct audio_device_info, list);
        if (item != NULL && compare_device_type(d2, item->type))
            return true;
    }
    return false;
}

/*
 * Returns all device types from list in bitfield
 * ToDo: Use of this function is not recommended.
 * It has been introduced for compatibility with legacy functions.
 * This can be removed once audio HAL switches to device
 * list usage for all audio extensions.
 */
audio_devices_t get_device_types(struct listnode *devices)
{
    struct listnode *node;
    struct audio_device_info *item = NULL;
    audio_devices_t device_type = AUDIO_DEVICE_NONE;

    if (devices == NULL)
        return AUDIO_DEVICE_NONE;

    list_for_each (node, devices) {
        item = node_to_item(node, struct audio_device_info, list);
        if (item != NULL)
            device_type |= item->type;
    }
    return device_type;
}

/*
 * If single device in devices list is equal to passed type
 * type should represent a single device.
 */
bool is_single_device_type_equal(struct listnode *devices,
                                 audio_devices_t type)
{
    struct listnode *node;
    struct audio_device_info *item = NULL;

    if (devices == NULL)
        return false;

    if (list_length(devices) == 1) {
        node = devices->next;
        item = node_to_item(node, struct audio_device_info, list);
        if (item != NULL && (item->type == type))
            return true;
    }
    return false;
}

/*
 * Returns true if lists are equal in terms of device type
 * ToDO: Check if device addresses are also equal in the future
 */
bool compare_devices(struct listnode *d1, struct listnode *d2)
{
    struct listnode *node;
    struct audio_device_info *item = NULL;

    if (d1 == NULL && d2 == NULL)
        return true;

    if (d1 == NULL || d2 == NULL ||
            (list_length(d1) != list_length(d2)))
        return false;

    list_for_each (node, d1) {
        item = node_to_item(node, struct audio_device_info, list);
        if (item != NULL && !compare_device_type(d2, item->type))
            return false;
    }
    return true;
}

/*
 * Add or remove device from list denoted by head
 */
int update_device_list(struct listnode *head, audio_devices_t type,
                       const char* address, bool add_device)
{
    struct listnode *node;
    struct audio_device_info *item = NULL;
    struct audio_device_info *device = NULL;
    int ret = 0;

    if (head == NULL)
        goto done;

    if (type == AUDIO_DEVICE_NONE) {
        ALOGE("%s: Invalid device: %#x", __func__, type);
        ret = -EINVAL;
        goto done;
    }

    list_for_each (node, head) {
        item = node_to_item(node, struct audio_device_info, list);
        if (item != NULL && (item->type == type)) {
            device = item;
            break;
        }
    }

    if (add_device) {
        if (device == NULL) {
            device = (struct audio_device_info *)
                        calloc (1, sizeof(struct audio_device_info));
            if (!device) {
                ALOGE("%s: Cannot allocate memory for device_info", __func__);
                ret = -ENOMEM;
                goto done;
            }
            device->type = type;
            list_add_tail(head, &device->list);
        }
        strlcpy(device->address, address, AUDIO_DEVICE_MAX_ADDRESS_LEN);
        ALOGV("%s: Added device type %#x, address %s", __func__, type, address);
    } else {
        if (device != NULL) {
            list_remove(&device->list);
            free(device);
            ALOGV("%s: Removed device type %#x, address %s", __func__, type, address);
        }
    }
done:
    return ret;
}

/*
 * Assign source device list to destination device list
 * Operation: dest list = source list
 */
int assign_devices(struct listnode *dest, const struct listnode *source)
{
    struct listnode *node;
    struct audio_device_info *item = NULL;
    int ret = 0;

    if (source == NULL || dest == NULL)
        return ret;

    if (!list_empty(dest))
        clear_devices(dest);

    list_for_each (node, source) {
        item = node_to_item(node, struct audio_device_info, list);
        if (item != NULL)
            ret = update_device_list(dest, item->type, item->address, true);
    }
    return ret;
}

/*
 * Assign output devices from source device list to destination device list
 * Operation: dest devices = source output devices
 */
int assign_output_devices(struct listnode *dest, const struct listnode *source)
{
    struct listnode *node;
    struct audio_device_info *item = NULL;
    int ret = 0;

    if (source == NULL || dest == NULL)
        return ret;

    if (!list_empty(dest))
        clear_devices(dest);

    list_for_each (node, source) {
        item = node_to_item(node, struct audio_device_info, list);
        if (item != NULL && audio_is_output_device(item->type))
            ret = update_device_list(dest, item->type, item->address, true);
    }
    return ret;
}

/*
 * Clear device list and replace it with single device
 */
int reassign_device_list(struct listnode *device_list,
                            audio_devices_t type, char *address)
{
    if (device_list == NULL)
        return 0;

    if (!list_empty(device_list))
        clear_devices(device_list);

    return update_device_list(device_list, type, address, true);
}

/*
 * Append source devices to destination devices
 * Operation: dest devices |= source devices
 */
int append_devices(struct listnode *dest, const struct listnode *source)
{
    struct listnode *node;
    struct audio_device_info *item = NULL;
    int ret = 0;

    if (source == NULL || dest == NULL)
        return ret;

    list_for_each (node, source) {
        item = node_to_item(node, struct audio_device_info, list);
        if (item != NULL)
            ret = update_device_list(dest, item->type, item->address, true);
    }
    return ret;
}
