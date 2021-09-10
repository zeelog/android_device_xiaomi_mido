/*
 * Copyright (c) 2017, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
 * Copyright (C) 2011 The Android Open Source Project
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


#ifndef ANDROID_AUDIO_QAHW_EFFECT_H
#define ANDROID_AUDIO_QAHW_EFFECT_H

#include <errno.h>
#include <stdint.h>
#include <strings.h>
#include <sys/cdefs.h>
#include <sys/types.h>

#include <cutils/bitops.h>

#include <system/audio.h>

__BEGIN_DECLS

#define QAHW_EFFECT_API_VERSION_0_0 QAHW_MAKE_API_VERSION(0, 0)
#define QAHW_EFFECT_API_VERSION_MIN QAHW_EFFECT_API_VERSION_0_0

/////////////////////////////////////////////////
//      Common Definitions
/////////////////////////////////////////////////

//
//--- Effect descriptor structure qahw_effect_descriptor_t
//

// Unique effect ID (can be generated from the following site:
//  http://www.itu.int/ITU-T/asn1/uuid.html)
// This format is used for both "type" and "uuid" fields of the effect descriptor structure.
// - When used for effect type and the engine is implementing and effect corresponding to a standard
// OpenSL ES interface, this ID must be the one defined in OpenSLES_IID.h for that interface.
// - When used as uuid, it should be a unique UUID for this particular implementation.
typedef struct qahw_effect_uuid_s {
    uint32_t timeLow;
    uint16_t timeMid;
    uint16_t timeHiAndVersion;
    uint16_t clockSeq;
    uint8_t node[6];
} qahw_effect_uuid_t;

// Maximum length of character strings in structures defines by this API.
#define QAHW_EFFECT_STRING_LEN_MAX 64

// NULL UUID definition (matches SL_IID_NULL_)
#define QAHW_EFFECT_UUID_INITIALIZER { 0xec7178ec, 0xe5e1, 0x4432, 0xa3f4, \
                                     { 0x46, 0x57, 0xe6, 0x79, 0x52, 0x10 } }
static const qahw_effect_uuid_t QAHW_EFFECT_UUID_NULL_ = QAHW_EFFECT_UUID_INITIALIZER;
static const qahw_effect_uuid_t * const QAHW_EFFECT_UUID_NULL = &QAHW_EFFECT_UUID_NULL_;
static const char * const QAHW_EFFECT_UUID_NULL_STR = "ec7178ec-e5e1-4432-a3f4-4657e6795210";


// The effect descriptor contains necessary information to facilitate the enumeration of the effect
// engines present in a library.
typedef struct qahw_effect_descriptor_s {
    qahw_effect_uuid_t type;     // UUID of to the OpenSL ES interface implemented by this effect
    qahw_effect_uuid_t uuid;     // UUID for this particular implementation
    uint32_t      apiVersion;    // Version of the effect control API implemented
    uint32_t      flags;         // effect engine capabilities/requirements flags (see below)
    uint16_t      cpuLoad;       // CPU load indication (see below)
    uint16_t      memoryUsage;   // Data Memory usage (see below)
    char    name[QAHW_EFFECT_STRING_LEN_MAX];   // human readable effect name
    char    implementor[QAHW_EFFECT_STRING_LEN_MAX];    // human readable effect implementor name
} qahw_effect_descriptor_t;

#define QAHW_EFFECT_MAKE_API_VERSION(M, m)  (((M)<<16) | ((m) & 0xFFFF))
#define QAHW_EFFECT_API_VERSION_MAJOR(v)    ((v)>>16)
#define QAHW_EFFECT_API_VERSION_MINOR(v)    ((m) & 0xFFFF)


/////////////////////////////////////////////////
//      Effect control interface
/////////////////////////////////////////////////

// Effect control interface version 2.0
#define QAHW_EFFECT_CONTROL_API_VERSION QAHW_EFFECT_MAKE_API_VERSION(2,0)

typedef void* qahw_effect_handle_t;


// Forward definition of type qahw_audio_buffer_t
typedef struct qahw_audio_buffer_s qahw_audio_buffer_t;


//
//--- Standardized command codes for command() function
//
enum qahw_effect_command_e {
   QAHW_EFFECT_CMD_INIT,                 // initialize effect engine
   QAHW_EFFECT_CMD_SET_CONFIG,           // configure effect engine (see effect_config_t)
   QAHW_EFFECT_CMD_RESET,                // reset effect engine
   QAHW_EFFECT_CMD_ENABLE,               // enable effect process
   QAHW_EFFECT_CMD_DISABLE,              // disable effect process
   QAHW_EFFECT_CMD_SET_PARAM,            // set parameter immediately (see effect_param_t)
   QAHW_EFFECT_CMD_SET_PARAM_DEFERRED,   // set parameter deferred
   QAHW_EFFECT_CMD_SET_PARAM_COMMIT,     // commit previous set parameter deferred
   QAHW_EFFECT_CMD_GET_PARAM,            // get parameter
   QAHW_EFFECT_CMD_SET_DEVICE,           // set audio device (see audio.h, audio_devices_t)
   QAHW_EFFECT_CMD_SET_VOLUME,           // set volume
   QAHW_EFFECT_CMD_SET_AUDIO_MODE,       // set the audio mode (normal, ring, ...)
   QAHW_EFFECT_CMD_SET_CONFIG_REVERSE,   // configure effect engine reverse stream
                                         // (see effect_config_t)
   QAHW_EFFECT_CMD_SET_INPUT_DEVICE,     // set capture device (see audio.h, audio_devices_t)
   QAHW_EFFECT_CMD_GET_CONFIG,           // read effect engine configuration
   QAHW_EFFECT_CMD_GET_CONFIG_REVERSE,   // read configure effect engine reverse stream
                                         // configuration
   QAHW_EFFECT_CMD_GET_FEATURE_SUPPORTED_CONFIGS, // get all supported configurations for
                                                  // a feature.
   QAHW_EFFECT_CMD_GET_FEATURE_CONFIG,   // get current feature configuration
   QAHW_EFFECT_CMD_SET_FEATURE_CONFIG,   // set current feature configuration
   QAHW_EFFECT_CMD_SET_AUDIO_SOURCE,     // set the audio source (see audio.h, audio_source_t)
   QAHW_EFFECT_CMD_OFFLOAD,              // set if effect thread is an offload one,
                                         // send the ioHandle of the effect thread
   QAHW_EFFECT_CMD_FIRST_PROPRIETARY = 0x10000 // first proprietary command code
};

//==================================================================================================
// command: QAHW_EFFECT_CMD_INIT
//--------------------------------------------------------------------------------------------------
// description:
//  Initialize effect engine: All configurations return to default
//--------------------------------------------------------------------------------------------------
// command format:
//  size: 0
//  data: N/A
//--------------------------------------------------------------------------------------------------
// reply format:
//  size: sizeof(int)
//  data: status
//==================================================================================================
// command: QAHW_EFFECT_CMD_SET_CONFIG
//--------------------------------------------------------------------------------------------------
// description:
//  Apply new audio parameters configurations for input and output buffers
//--------------------------------------------------------------------------------------------------
// command format:
//  size: sizeof(effect_config_t)
//  data: effect_config_t
//--------------------------------------------------------------------------------------------------
// reply format:
//  size: sizeof(int)
//  data: status
//==================================================================================================
// command: QAHW_EFFECT_CMD_RESET
//--------------------------------------------------------------------------------------------------
// description:
//  Reset the effect engine. Keep configuration but resets state and buffer content
//--------------------------------------------------------------------------------------------------
// command format:
//  size: 0
//  data: N/A
//--------------------------------------------------------------------------------------------------
// reply format:
//  size: 0
//  data: N/A
//==================================================================================================
// command: QAHW_EFFECT_CMD_ENABLE
//--------------------------------------------------------------------------------------------------
// description:
//  Enable the process. Called by the framework before the first call to process()
//--------------------------------------------------------------------------------------------------
// command format:
//  size: 0
//  data: N/A
//--------------------------------------------------------------------------------------------------
// reply format:
//  size: sizeof(int)
//  data: status
//==================================================================================================
// command: QAHW_EFFECT_CMD_DISABLE
//--------------------------------------------------------------------------------------------------
// description:
//  Disable the process. Called by the framework after the last call to process()
//--------------------------------------------------------------------------------------------------
// command format:
//  size: 0
//  data: N/A
//--------------------------------------------------------------------------------------------------
// reply format:
//  size: sizeof(int)
//  data: status
//==================================================================================================
// command: QAHW_EFFECT_CMD_SET_PARAM
//--------------------------------------------------------------------------------------------------
// description:
//  Set a parameter and apply it immediately
//--------------------------------------------------------------------------------------------------
// command format:
//  size: sizeof(effect_param_t) + size of param and value
//  data: effect_param_t + param + value. See effect_param_t definition below for value offset
//--------------------------------------------------------------------------------------------------
// reply format:
//  size: sizeof(int)
//  data: status
//==================================================================================================
// command: QAHW_EFFECT_CMD_SET_PARAM_DEFERRED
//--------------------------------------------------------------------------------------------------
// description:
//  Set a parameter but apply it only when receiving QAHW_EFFECT_CMD_SET_PARAM_COMMIT command
//--------------------------------------------------------------------------------------------------
// command format:
//  size: sizeof(effect_param_t) + size of param and value
//  data: effect_param_t + param + value. See effect_param_t definition below for value offset
//--------------------------------------------------------------------------------------------------
// reply format:
//  size: 0
//  data: N/A
//==================================================================================================
// command: QAHW_EFFECT_CMD_SET_PARAM_COMMIT
//--------------------------------------------------------------------------------------------------
// description:
//  Apply all previously received QAHW_EFFECT_CMD_SET_PARAM_DEFERRED commands
//--------------------------------------------------------------------------------------------------
// command format:
//  size: 0
//  data: N/A
//--------------------------------------------------------------------------------------------------
// reply format:
//  size: sizeof(int)
//  data: status
//==================================================================================================
// command: QAHW_EFFECT_CMD_GET_PARAM
//--------------------------------------------------------------------------------------------------
// description:
//  Get a parameter value
//--------------------------------------------------------------------------------------------------
// command format:
//  size: sizeof(effect_param_t) + size of param
//  data: effect_param_t + param
//--------------------------------------------------------------------------------------------------
// reply format:
//  size: sizeof(effect_param_t) + size of param and value
//  data: effect_param_t + param + value. See effect_param_t definition below for value offset
//==================================================================================================
// command: QAHW_EFFECT_CMD_SET_DEVICE
//--------------------------------------------------------------------------------------------------
// description:
//  Set the rendering device the audio output path is connected to. See audio.h, audio_devices_t
//  for device values.
//  The effect implementation must set QAHW_EFFECT_FLAG_DEVICE_IND flag in its descriptor to
//  receive this command when the device changes
//--------------------------------------------------------------------------------------------------
// command format:
//  size: sizeof(uint32_t)
//  data: uint32_t
//--------------------------------------------------------------------------------------------------
// reply format:
//  size: 0
//  data: N/A
//==================================================================================================
// command: QAHW_EFFECT_CMD_SET_VOLUME
//--------------------------------------------------------------------------------------------------
// description:
//  Set and get volume. Used by audio framework to delegate volume control to effect engine.
//  The effect implementation must set QAHW_EFFECT_FLAG_VOLUME_IND or QAHW_EFFECT_FLAG_VOLUME_CTRL
//  flag in its descriptor to receive this command before every call to process() function
//  If QAHW_EFFECT_FLAG_VOLUME_CTRL flag is set in the effect descriptor, the effect engine must
//  return the volume that should be applied before the effect is processed. The overall volume
//  (the volume actually applied by the effect engine multiplied by the returned value) should
//  match the value indicated in the command.
//--------------------------------------------------------------------------------------------------
// command format:
//  size: n * sizeof(uint32_t)
//  data: volume for each channel defined in effect_config_t for output buffer expressed in
//      8.24 fixed point format
//--------------------------------------------------------------------------------------------------
// reply format:
//  size: n * sizeof(uint32_t) / 0
//  data: - if QAHW_EFFECT_FLAG_VOLUME_CTRL is set in effect descriptor:
//              volume for each channel defined in effect_config_t for output buffer expressed in
//              8.24 fixed point format
//        - if QAHW_EFFECT_FLAG_VOLUME_CTRL is not set in effect descriptor:
//              N/A
//  It is legal to receive a null pointer as pReplyData in which case the effect framework has
//  delegated volume control to another effect
//==================================================================================================
// command: QAHW_EFFECT_CMD_SET_AUDIO_MODE
//--------------------------------------------------------------------------------------------------
// description:
//  Set the audio mode. The effect implementation must set QAHW_EFFECT_FLAG_AUDIO_MODE_IND flag
//  in its descriptor to receive this command when the audio mode changes.
//--------------------------------------------------------------------------------------------------
// command format:
//  size: sizeof(uint32_t)
//  data: audio_mode_t
//--------------------------------------------------------------------------------------------------
// reply format:
//  size: 0
//  data: N/A
//==================================================================================================
// command: QAHW_EFFECT_CMD_SET_CONFIG_REVERSE
//--------------------------------------------------------------------------------------------------
// description:
//  Apply new audio parameters configurations for input and output buffers of reverse stream.
//  An example of reverse stream is the echo reference supplied to an Acoustic Echo Canceler.
//--------------------------------------------------------------------------------------------------
// command format:
//  size: sizeof(effect_config_t)
//  data: effect_config_t
//--------------------------------------------------------------------------------------------------
// reply format:
//  size: sizeof(int)
//  data: status
//==================================================================================================
// command: QAHW_EFFECT_CMD_SET_INPUT_DEVICE
//--------------------------------------------------------------------------------------------------
// description:
//  Set the capture device the audio input path is connected to. See audio.h, audio_devices_t
//  for device values.
//  The effect implementation must set QAHW_EFFECT_FLAG_DEVICE_IND flag in its descriptor to
//  receive this command when the device changes
//--------------------------------------------------------------------------------------------------
// command format:
//  size: sizeof(uint32_t)
//  data: uint32_t
//--------------------------------------------------------------------------------------------------
// reply format:
//  size: 0
//  data: N/A
//==================================================================================================
// command: QAHW_EFFECT_CMD_GET_CONFIG
//--------------------------------------------------------------------------------------------------
// description:
//  Read audio parameters configurations for input and output buffers
//--------------------------------------------------------------------------------------------------
// command format:
//  size: 0
//  data: N/A
//--------------------------------------------------------------------------------------------------
// reply format:
//  size: sizeof(effect_config_t)
//  data: effect_config_t
//==================================================================================================
// command: QAHW_EFFECT_CMD_GET_CONFIG_REVERSE
//--------------------------------------------------------------------------------------------------
// description:
//  Read audio parameters configurations for input and output buffers of reverse stream
//--------------------------------------------------------------------------------------------------
// command format:
//  size: 0
//  data: N/A
//--------------------------------------------------------------------------------------------------
// reply format:
//  size: sizeof(effect_config_t)
//  data: effect_config_t
//==================================================================================================
// command: QAHW_EFFECT_CMD_GET_FEATURE_SUPPORTED_CONFIGS
//--------------------------------------------------------------------------------------------------
// description:
//  Queries for supported configurations for a particular feature (e.g. get the supported
// combinations of main and auxiliary channels for a noise suppressor).
// The command parameter is the feature identifier (See effect_feature_e for a list of defined
// features) followed by the maximum number of configuration descriptor to return.
// The reply is composed of:
//  - status (uint32_t):
//          - 0 if feature is supported
//          - -ENOSYS if the feature is not supported,
//          - -ENOMEM if the feature is supported but the total number of supported configurations
//          exceeds the maximum number indicated by the caller.
//  - total number of supported configurations (uint32_t)
//  - an array of configuration descriptors.
// The actual number of descriptors returned must not exceed the maximum number indicated by
// the caller.
//--------------------------------------------------------------------------------------------------
// command format:
//  size: 2 x sizeof(uint32_t)
//  data: effect_feature_e + maximum number of configurations to return
//--------------------------------------------------------------------------------------------------
// reply format:
//  size: 2 x sizeof(uint32_t) + n x sizeof (<config descriptor>)
//  data: status + total number of configurations supported + array of n config descriptors
//==================================================================================================
// command: QAHW_EFFECT_CMD_GET_FEATURE_CONFIG
//--------------------------------------------------------------------------------------------------
// description:
//  Retrieves current configuration for a given feature.
// The reply status is:
//      - 0 if feature is supported
//      - -ENOSYS if the feature is not supported,
//--------------------------------------------------------------------------------------------------
// command format:
//  size: sizeof(uint32_t)
//  data: effect_feature_e
//--------------------------------------------------------------------------------------------------
// reply format:
//  size: sizeof(uint32_t) + sizeof (<config descriptor>)
//  data: status + config descriptor
//==================================================================================================
// command: QAHW_EFFECT_CMD_SET_FEATURE_CONFIG
//--------------------------------------------------------------------------------------------------
// description:
//  Sets current configuration for a given feature.
// The reply status is:
//      - 0 if feature is supported
//      - -ENOSYS if the feature is not supported,
//      - -EINVAL if the configuration is invalid
//--------------------------------------------------------------------------------------------------
// command format:
//  size: sizeof(uint32_t) + sizeof (<config descriptor>)
//  data: effect_feature_e + config descriptor
//--------------------------------------------------------------------------------------------------
// reply format:
//  size: sizeof(uint32_t)
//  data: status
//==================================================================================================
// command: QAHW_EFFECT_CMD_SET_AUDIO_SOURCE
//--------------------------------------------------------------------------------------------------
// description:
//  Set the audio source the capture path is configured for (Camcorder, voice recognition...).
//  See audio.h, audio_source_t for values.
//--------------------------------------------------------------------------------------------------
// command format:
//  size: sizeof(uint32_t)
//  data: uint32_t
//--------------------------------------------------------------------------------------------------
// reply format:
//  size: 0
//  data: N/A
//==================================================================================================
// command: QAHW_EFFECT_CMD_OFFLOAD
//--------------------------------------------------------------------------------------------------
// description:
//  1.indicate if the playback thread the effect is attached to is offloaded or not
//  2.update the io handle of the playback thread the effect is attached to
//--------------------------------------------------------------------------------------------------
// command format:
//  size: sizeof(effect_offload_param_t)
//  data: effect_offload_param_t
//--------------------------------------------------------------------------------------------------
// reply format:
//  size: sizeof(uint32_t)
//  data: uint32_t
//--------------------------------------------------------------------------------------------------
// command: QAHW_EFFECT_CMD_FIRST_PROPRIETARY
//--------------------------------------------------------------------------------------------------
// description:
//  All proprietary effect commands must use command codes above this value. The size and format of
//  command and response fields is free in this case
//==================================================================================================


// Audio buffer descriptor used by process(), bufferProvider() functions and buffer_config_t
// structure. Multi-channel audio is always interleaved. The channel order is from LSB to MSB with
// regard to the channel mask definition in audio.h, audio_channel_mask_t e.g :
// Stereo: left, right
// 5 point 1: front left, front right, front center, low frequency, back left, back right
// The buffer size is expressed in frame count, a frame being composed of samples for all
// channels at a given time. Frame size for unspecified format (AUDIO_FORMAT_OTHER) is 8 bit by
// definition
struct qahw_audio_buffer_s {
    size_t   frameCount;        // number of frames in buffer
    union {
        void*       raw;        // raw pointer to start of buffer
        int32_t*    s32;        // pointer to signed 32 bit data at start of buffer
        int16_t*    s16;        // pointer to signed 16 bit data at start of buffer
        uint8_t*    u8;         // pointer to unsigned 8 bit data at start of buffer
    };
};

// The buffer_provider_s structure contains functions that can be used
// by the effect engine process() function to query and release input
// or output audio buffer.
// The getBuffer() function is called to retrieve a buffer where data
// should read from or written to by process() function.
// The releaseBuffer() function MUST be called when the buffer retrieved
// with getBuffer() is not needed anymore.
// The process function should use the buffer provider mechanism to retrieve
// input or output buffer if the in_buffer or out_buffer passed as argument is NULL
// and the buffer configuration (buffer_config_t) given by the QAHW_EFFECT_CMD_SET_CONFIG
// command did not specify an audio buffer.

typedef int32_t (* qahw_buffer_function_t)(void *cookie, qahw_audio_buffer_t *buffer);

typedef struct qahw_buffer_provider_s {
    qahw_buffer_function_t getBuffer;       // retrieve next buffer
    qahw_buffer_function_t releaseBuffer;   // release used buffer
    void       *cookie;                // for use by client of buffer provider functions
} qahw_buffer_provider_t;


// The qahw_buffer_config_s structure specifies the input or output audio format
// to be used by the effect engine. It is part of the effect_config_t
// structure that defines both input and output buffer configurations and is
// passed by the QAHW_EFFECT_CMD_SET_CONFIG or QAHW_EFFECT_CMD_SET_CONFIG_REVERSE command.
typedef struct qahw_buffer_config_s {
    qahw_audio_buffer_t  buffer; // buffer for use by process() function if not passed explicitly
    uint32_t   samplingRate;     // sampling rate
    uint32_t   channels;         // channel mask (see audio_channel_mask_t in audio.h)
    qahw_buffer_provider_t bufferProvider;   // buffer provider
    uint8_t    format;           // Audio format (see audio_format_t in audio.h)
    uint8_t    accessMode;       // read/write or accumulate in buffer (qahw_effect_buffer_access_e)
    uint16_t   mask;             // indicates which of the above fields is valid
} qahw_buffer_config_t;

// Values for "accessMode" field of buffer_config_t:
//   overwrite, read only, accumulate (read/modify/write)
enum qahw_effect_buffer_access_e {
    QAHW_EFFECT_BUFFER_ACCESS_WRITE,
    QAHW_EFFECT_BUFFER_ACCESS_READ,
    QAHW_EFFECT_BUFFER_ACCESS_ACCUMULATE

};

// feature identifiers for QAHW_EFFECT_CMD_GET_FEATURE_SUPPORTED_CONFIGS command
enum qahw_effect_feature_e {
    QAHW_EFFECT_FEATURE_AUX_CHANNELS, // supports auxiliary channels
                                      // (e.g. dual mic noise suppressor)
    QAHW_EFFECT_FEATURE_CNT
};

// QAHW_EFFECT_FEATURE_AUX_CHANNELS feature configuration descriptor. Describe a combination
// of main and auxiliary channels supported
typedef struct qahw_channel_config_s {
    audio_channel_mask_t main_channels; // channel mask for main channels
    audio_channel_mask_t aux_channels;  // channel mask for auxiliary channels
} qahw_channel_config_t;


// Values for bit field "mask" in buffer_config_t. If a bit is set, the corresponding field
// in buffer_config_t must be taken into account when executing the QAHW_EFFECT_CMD_SET_CONFIG
// command
#define QAHW_EFFECT_CONFIG_BUFFER    0x0001  // buffer field must be taken into account
#define QAHW_EFFECT_CONFIG_SMP_RATE  0x0002  // samplingRate field must be taken into account
#define QAHW_EFFECT_CONFIG_CHANNELS  0x0004  // channels field must be taken into account
#define QAHW_EFFECT_CONFIG_FORMAT    0x0008  // format field must be taken into account
#define QAHW_EFFECT_CONFIG_ACC_MODE  0x0010  // accessMode field must be taken into account
#define QAHW_EFFECT_CONFIG_PROVIDER  0x0020  // bufferProvider field must be taken into account
#define QAHW_EFFECT_CONFIG_ALL (QAHW_EFFECT_CONFIG_BUFFER | QAHW_EFFECT_CONFIG_SMP_RATE | \
                                QAHW_EFFECT_CONFIG_CHANNELS | QAHW_EFFECT_CONFIG_FORMAT | \
                                QAHW_EFFECT_CONFIG_ACC_MODE | QAHW_EFFECT_CONFIG_PROVIDER)


// effect_config_s structure describes the format of the pCmdData argument of
// QAHW_EFFECT_CMD_SET_CONFIG command to configure audio parameters and buffers for effect
// engine input and output.
typedef struct qahw_effect_config_s {
    qahw_buffer_config_t   input_cfg;
    qahw_buffer_config_t   output_cfg;
} qahw_effect_config_t;


// effect_param_s structure describes the format of the pCmdData argument of
// QAHW_EFFECT_CMD_SET_PARAM command and pCmdData and pReplyData of QAHW_EFFECT_CMD_GET_PARAM
// command. psize and vsize represent the actual size of parameter and value.
//
// NOTE: the start of value field inside the data field is always on a 32 bit boundary:
//
//  +-----------+
//  | status    | sizeof(int)
//  +-----------+
//  | psize     | sizeof(int)
//  +-----------+
//  | vsize     | sizeof(int)
//  +-----------+
//  |           |   |           |
//  ~ parameter ~   > psize     |
//  |           |   |           >  ((psize - 1)/sizeof(int) + 1) * sizeof(int)
//  +-----------+               |
//  | padding   |               |
//  +-----------+
//  |           |   |
//  ~ value     ~   > vsize
//  |           |   |
//  +-----------+

typedef struct qahw_effect_param_s {
    int32_t     status;     // Transaction status (unused for command, used for reply)
    uint32_t    psize;      // Parameter size
    uint32_t    vsize;      // Value size
    char        data[];     // Start of Parameter + Value data
} qahw_effect_param_t;

// structure used by QAHW_EFFECT_CMD_OFFLOAD command
typedef struct qahw_effect_offload_param_s {
    bool isOffload;         // true if the playback thread the effect is attached to is offloaded
    int ioHandle;           // io handle of the playback thread the effect is attached to
} qahw_effect_offload_param_t;


/////////////////////////////////////////////////
//      Effect library interface
/////////////////////////////////////////////////

// Effect library interface version 3.0
// Note that EffectsFactory.c only checks the major version component, so changes to the minor
// number can only be used for fully backwards compatible changes
#define QAHW_EFFECT_LIBRARY_API_VERSION QAHW_EFFECT_MAKE_API_VERSION(3, 0)

typedef void* qahw_effect_lib_handle_t;

////////////////////////////////////////////////////////////////////////////////
//
//    Function:       qahw_effect_load_library
//
//    Description:    Loads an effect library
//
//    Input:
//        lib_name:   Effect library name.
//
//    Output:
//        returned value:    NULL       if fails to load library.
//                           valid effect library handle
//
////////////////////////////////////////////////////////////////////////////////
qahw_effect_lib_handle_t qahw_effect_load_library(const char *lib_name);

////////////////////////////////////////////////////////////////////////////////
//
//    Function:       qahw_effect_unload_library
//
//    Description:    Unload the audio effect library
//
//    Input:
//        handle:     Effect library handle.
//
//    Output:
//        returned value:    0          successful operation.
//                          -EINVAL     invalid effect library handle
//
////////////////////////////////////////////////////////////////////////////////
int32_t qahw_effect_unload_library(qahw_effect_lib_handle_t handle);

////////////////////////////////////////////////////////////////////////////////
//
//    Function:       qahw_effect_create
//
//    Description:    Creates an effect engine of the specified implementation uuid and
//          returns an effect control interface on this engine. The function will allocate the
//          resources for an instance of the requested effect engine and return
//          a handle on the effect control interface.
//
//    Input:
//          handle:     handle on the effect library.
//          uuid:       pointer to the effect uuid.
//          sessionId:  audio session to which this effect instance will be attached.
//                      All effects created with the same session ID are connected in series
//                      and process the same signal stream. Knowing that two effects are part
//                      of the same effect chain can help the library implement some kind of
//                      optimizations.
//          io_handle:  identifies the output or input stream this effect is directed to in
//                      audio HAL.
//                      For future use especially with tunneled HW accelerated effects
//
//    Input/Output:
//          effect_handle:   address where to return the effect interface handle.
//
//    Output:
//        returned value:    0          successful operation.
//                          -ENODEV     library failed to initialize
//                          -EINVAL     invalid pEffectUuid or effect_handle
//                          -ENOENT     no effect with this uuid found
//        *effect_handle:    updated with the effect interface handle.
//
////////////////////////////////////////////////////////////////////////////////
int32_t qahw_effect_create(qahw_effect_lib_handle_t handle,
                           const qahw_effect_uuid_t *uuid,
                           int32_t io_handle,
                           qahw_effect_handle_t *effect_handle);

////////////////////////////////////////////////////////////////////////////////
//
//    Function:       qahw_effect_release
//
//    Description:    Releases the effect engine whose handle is given as argument.
//          All resources allocated to this particular instance of the effect are
//          released.
//
//    Input:
//          handle:         handle on the effect library.
//          effect_handle:  handle on the effect interface to be released.
//
//    Output:
//        returned value:    0          successful operation.
//                          -EINVAL     invalid interface handle
//
////////////////////////////////////////////////////////////////////////////////
int32_t qahw_effect_release(qahw_effect_lib_handle_t handle,
                            qahw_effect_handle_t effect_handle);

////////////////////////////////////////////////////////////////////////////////
//
//    Function:        qahw_effect_get_descriptor
//
//    Description:     Returns the descriptor of the effect engine which implementation UUID is
//          given as argument.
//
//    Input/Output:
//          handle:         handle on the effect library.
//          uuid:           pointer to the effect uuid.
//          effect_desc:    address where to return the effect descriptor.
//
//    Output:
//        returned value:    0          successful operation.
//                          -ENODEV     library failed to initialize
//                          -EINVAL     invalid effect_desc or uuid
//        *effect_desc:     updated with the effect descriptor.
//
////////////////////////////////////////////////////////////////////////////////
int32_t qahw_effect_get_descriptor(qahw_effect_lib_handle_t handle,
                                   const qahw_effect_uuid_t *uuid,
                                   qahw_effect_descriptor_t *effect_desc);

////////////////////////////////////////////////////////////////////////////////
//
//    Function:        qahw_effect_get_version
//
//    Description:     Get version of IOT effect APIs.
//
//    Output:
//        returned value:    version number
//
////////////////////////////////////////////////////////////////////////////////
int32_t qahw_effect_get_version();

////////////////////////////////////////////////////////////////////////////////
//
//    Function:        qahw_effect_process
//
//    Description:     Effect process function. Takes input samples as specified
//          (count and location) in input buffer descriptor and output processed
//          samples as specified in output buffer descriptor. If the buffer descriptor
//          is not specified the function must use either the buffer or the
//          buffer provider function installed by the QAHW_EFFECT_CMD_SET_CONFIG command.
//          The effect framework will call the process() function after the QAHW_EFFECT_CMD_ENABLE
//          command is received and until the QAHW_EFFECT_CMD_DISABLE is received. When the engine
//          receives the QAHW_EFFECT_CMD_DISABLE command it should turn off the effect gracefully
//          and when done indicate that it is OK to stop calling the process() function by
//          returning the -ENODATA status.
//
//    NOTE: the process() function implementation should be "real-time safe" that is
//      it should not perform blocking calls: malloc/free, sleep, read/write/open/close,
//      pthread_cond_wait/pthread_mutex_lock...
//
//    Input:
//          self:       handle to the effect interface this function is called on.
//          in_buffer:  buffer descriptor indicating where to read samples to process.
//                      If NULL, use the configuration passed by QAHW_EFFECT_CMD_SET_CONFIG command.
//          out_buffer: buffer descriptor indicating where to write processed samples.
//                      If NULL, use the configuration passed by QAHW_EFFECT_CMD_SET_CONFIG command.
//
//    Output:
//        returned value:    0 successful operation
//                          -ENODATA the engine has finished the disable phase and the framework
//                                  can stop calling process()
//                          -EINVAL invalid interface handle or
//                                  invalid input/output buffer description
////////////////////////////////////////////////////////////////////////////////
int32_t qahw_effect_process(qahw_effect_handle_t self,
                            qahw_audio_buffer_t *in_buffer,
                            qahw_audio_buffer_t *out_buffer);
////////////////////////////////////////////////////////////////////////////////
//
//    Function:       qahw_effect_command
//
//    Description:    Send a command and receive a response to/from effect engine.
//
//    Input:
//          self:       handle to the effect interface this function is called on.
//          cmd_code:   command code: the command can be a standardized command defined in
//                      qahw_effect_command_e (see below) or a proprietary command.
//          cmd_size:   size of command in bytes
//          cmd_data:   pointer to command data
//          reply_data: pointer to reply data
//
//    Input/Output:
//          reply_size: maximum size of reply data as input
//                      actual size of reply data as output
//
//    Output:
//          returned value: 0       successful operation
//                          -EINVAL invalid interface handle or
//                                  invalid command/reply size or format according to
//                                  command code
//              The return code should be restricted to indicate problems related to this API
//              specification. Status related to the execution of a particular command should be
//              indicated as part of the reply field.
//
//          *reply_data updated with command response
//
////////////////////////////////////////////////////////////////////////////////
int32_t qahw_effect_command(qahw_effect_handle_t self,
                            uint32_t cmd_code,
                            uint32_t cmd_size,
                            void *cmd_data,
                            uint32_t *reply_size,
                            void *reply_data);

////////////////////////////////////////////////////////////////////////////////
//
//    Function:       qahw_effect_process_reverse
//
//    Description:    Process reverse stream function. This function is used to pass
//          a reference stream to the effect engine. If the engine does not need a reference
//          stream, this function pointer can be set to NULL.
//          This function would typically implemented by an Echo Canceler.
//
//    Input:
//          self:       handle to the effect interface this function is called on.
//          in_buffer:  buffer descriptor indicating where to read samples to process.
//                      If NULL, use the configuration passed by
//                      QAHW_EFFECT_CMD_SET_CONFIG_REVERSE command.
//
//          out_buffer: buffer descriptor indicating where to write processed samples.
//                      If NULL, use the configuration passed by
//                      QAHW_EFFECT_CMD_SET_CONFIG_REVERSE command.
//              If the buffer and buffer provider in the configuration received by
//              QAHW_EFFECT_CMD_SET_CONFIG_REVERSE are also NULL, do not return modified reverse
//              stream data
//
//    Output:
//        returned value:    0 successful operation
//                          -ENODATA the engine has finished the disable phase and the framework
//                                  can stop calling process_reverse()
//                          -EINVAL invalid interface handle or
//                                  invalid input/output buffer description
////////////////////////////////////////////////////////////////////////////////
int32_t qahw_effect_process_reverse(qahw_effect_handle_t self,
                                    qahw_audio_buffer_t *in_buffer,
                                    qahw_audio_buffer_t *out_buffer);


__END_DECLS

#endif  // ANDROID_AUDIO_QAHW_EFFECT_H
