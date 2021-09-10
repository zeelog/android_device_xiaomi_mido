/*
 * Copyright (c) 2016-2019, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
 * Copyright (C) 2011 The Android Open Source Project *
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

#ifndef QTI_AUDIO_QAHW_API_H
#define QTI_AUDIO_QAHW_API_H

#include <stdint.h>
#include <strings.h>
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/time.h>
#include <cutils/bitops.h>
#include <system/audio.h>
#include "qahw_defs.h"

__BEGIN_DECLS
/*
 * Helper macros for module implementors.
 *
 * The derived modules should provide convenience macros for supported
 * versions so that implementations can explicitly specify module
 * versions at definition time.
 */

#define QAHW_MAKE_API_VERSION(maj,min) \
            ((((maj) & 0xff) << 8) | ((min) & 0xff))

/* First generation of audio devices had version hardcoded to 0. all devices with
 * versions < 1.0 will be considered of first generation API.
 */
#define QAHW_MODULE_API_VERSION_0_0 QAHW_MAKE_API_VERSION(0, 0)

/* Minimal QTI audio HAL version supported by the audio framework */
#define QAHW_MODULE_API_VERSION_MIN QAHW_MODULE_API_VERSION_0_0

/**
 * List of known audio HAL modules. This is the base name of the audio HAL
 * library composed of the "audio." prefix, one of the base names below and
 * a suffix specific to the device.
 * e.g: audio.primary.goldfish.so or audio.a2dp.default.so
 */

#define QAHW_MODULE_ID_PRIMARY     "audio.primary"
#define QAHW_MODULE_ID_A2DP        "audio.a2dp"
#define QAHW_MODULE_ID_USB         "audio.usb"

typedef void qahw_module_handle_t;
typedef void qahw_stream_handle_t;
typedef void (*audio_error_callback)(void* context);

/**************************************/
/* Output stream specific APIs **/

/*
 * This method creates and opens the audio hardware output stream.
 * The "address" parameter qualifies the "devices" audio device type if needed.
 * The format format depends on the device type:
 * - Bluetooth devices use the MAC address of the device in the form "00:11:22:AA:BB:CC"
 * - USB devices use the ALSA card and device numbers in the form  "card=X;device=Y"
 * - Other devices may use a number or any other string.
 */

int qahw_open_output_stream(qahw_module_handle_t *hw_module,
                            audio_io_handle_t handle,
                            audio_devices_t devices,
                            audio_output_flags_t flags,
                            struct audio_config *config,
                            qahw_stream_handle_t **out_handle,
                            const char *address);

int qahw_close_output_stream(qahw_stream_handle_t *out_handle);


/*
 * Return the sampling rate in Hz - eg. 44100.
 */
uint32_t qahw_out_get_sample_rate(const qahw_stream_handle_t *stream);

/*
 *  use set_parameters with key QAHW_PARAMETER_STREAM_SAMPLING_RATE
 */
int qahw_out_set_sample_rate(qahw_stream_handle_t *stream, uint32_t rate);

/*
 * Return size of input/output buffer in bytes for this stream - eg. 4800.
 * It should be a multiple of the frame size.  See also get_input_buffer_size.
 */
size_t qahw_out_get_buffer_size(const qahw_stream_handle_t *stream);

/*
 * Return the channel mask -
 *  e.g. AUDIO_CHANNEL_OUT_STEREO or AUDIO_CHANNEL_IN_STEREO
 */
audio_channel_mask_t qahw_out_get_channels(const qahw_stream_handle_t *stream);

/*
 * Return the audio format - e.g. AUDIO_FORMAT_PCM_16_BIT
 */
audio_format_t qahw_out_get_format(const qahw_stream_handle_t *stream);

/*
 * Put the audio hardware input/output into standby mode.
 * Driver should exit from standby mode at the next I/O operation.
 * Returns 0 on success and <0 on failure.
 */
int qahw_out_standby(qahw_stream_handle_t *stream);

/*
 * set/get audio stream parameters. The function accepts a list of
 * parameter key value pairs in the form: key1=value1;key2=value2;...
 *
 * Some keys are reserved for standard parameters (See AudioParameter class)
 *
 * If the implementation does not accept a parameter change while
 * the output is active but the parameter is acceptable otherwise, it must
 * return -ENOSYS.
 *
 * The audio flinger will put the stream in standby and then change the
 * parameter value.
 */
int qahw_out_set_parameters(qahw_stream_handle_t *stream, const char*kv_pairs);

/*
 * Returns a pointer to a heap allocated string. The caller is responsible
 * for freeing the memory for it using free().
 */
char* qahw_out_get_parameters(const qahw_stream_handle_t *stream,
                               const char *keys);

/* API to set playback stream specific config parameters */
int qahw_out_set_param_data(qahw_stream_handle_t *out_handle,
                            qahw_param_id param_id,
                            qahw_param_payload *payload);

/* API to get playback stream specific config parameters */
int qahw_out_get_param_data(qahw_stream_handle_t *out_handle,
                            qahw_param_id param_id,
                            qahw_param_payload *payload);

/*
 * Return the audio hardware driver estimated latency in milliseconds.
 */
uint32_t qahw_out_get_latency(const qahw_stream_handle_t *stream);

/*
 * Use this method in situations where audio mixing is done in the
 * hardware. This method serves as a direct interface with hardware,
 * allowing you to directly set the volume as apposed to via the framework.
 * This method might produce multiple PCM outputs or hardware accelerated
 * codecs, such as MP3 or AAC.
 */
int qahw_out_set_volume(qahw_stream_handle_t *stream, float left, float right);

/*
 * Write audio buffer present in meta_data starting from offset
 * along with timestamp to driver. Returns number of bytes
 * written or a negative status_t. If at least one frame was written successfully
 * prior to the error, it is suggested that the driver return that successful
 * (short) byte count and then return an error in the subsequent call.
 * timestamp is only sent driver is session has been opened with timestamp flag
 * otherwise its ignored.
 *
 * If set_callback() has previously been called to enable non-blocking mode
 * the write() is not allowed to block. It must write only the number of
 * bytes that currently fit in the driver/hardware buffer and then return
 * this byte count. If this is less than the requested write size the
 * callback function must be called when more space is available in the
 * driver/hardware buffer.
 */
ssize_t qahw_out_write(qahw_stream_handle_t *stream,
                       qahw_out_buffer_t *out_buf);

/*
 * return the number of audio frames written by the audio dsp to DAC since
 * the output has exited standby
 */
int qahw_out_get_render_position(const qahw_stream_handle_t *stream,
                                 uint32_t *dsp_frames);

/*
 * set the callback function for notifying completion of non-blocking
 * write and drain.
 * Calling this function implies that all future rite() and drain()
 * must be non-blocking and use the callback to signal completion.
 */
int qahw_out_set_callback(qahw_stream_handle_t *stream,
                          qahw_stream_callback_t callback,
                          void *cookie);

/*
 * Notifies to the audio driver to stop playback however the queued buffers are
 * retained by the hardware. Useful for implementing pause/resume. Empty implementation
 * if not supported however should be implemented for hardware with non-trivial
 * latency. In the pause state audio hardware could still be using power. User may
 * consider calling suspend after a timeout.
 *
 * Implementation of this function is mandatory for offloaded playback.
 */
int qahw_out_pause(qahw_stream_handle_t *out_handle);

/*
 * Notifies to the audio driver to resume playback following a pause.
 * Returns error if called without matching pause.
 *
 * Implementation of this function is mandatory for offloaded playback.
 */
int qahw_out_resume(qahw_stream_handle_t *out_handle);

/*
 * Requests notification when data buffered by the driver/hardware has
 * been played. If set_callback() has previously been called to enable
 * non-blocking mode, the drain() must not block, instead it should return
 * quickly and completion of the drain is notified through the callback.
 * If set_callback() has not been called, the drain() must block until
 * completion.
 * If type==AUDIO_DRAIN_ALL, the drain completes when all previously written
 * data has been played.
 * If type==AUDIO_DRAIN_EARLY_NOTIFY, the drain completes shortly before all
 * data for the current track has played to allow time for the framework
 * to perform a gapless track switch.
 *
 * Drain must return immediately on stop() and flush() call
 *
 * Implementation of this function is mandatory for offloaded playback.
 */
int qahw_out_drain(qahw_stream_handle_t *out_handle, qahw_drain_type_t type);

/*
 * Notifies to the audio driver to flush the queued data. Stream must already
 * be paused before calling flush().
 *
 * Implementation of this function is mandatory for offloaded playback.
 */
int qahw_out_flush(qahw_stream_handle_t *out_handle);

/*
 * Return a recent count of the number of audio frames presented to an external observer.
 * This excludes frames which have been written but are still in the pipeline.
 * The count is not reset to zero when output enters standby.
 * Also returns the value of CLOCK_MONOTONIC as of this presentation count.
 * The returned count is expected to be 'recent',
 * but does not need to be the most recent possible value.
 * However, the associated time should correspond to whatever count is returned.
 * Example:  assume that N+M frames have been presented, where M is a 'small' number.
 * Then it is permissible to return N instead of N+M,
 * and the timestamp should correspond to N rather than N+M.
 * The terms 'recent' and 'small' are not defined.
 * They reflect the quality of the implementation.
 *
 * 3.0 and higher only.
 */
int qahw_out_get_presentation_position(const qahw_stream_handle_t *out_handle,
                                       uint64_t *frames, struct timespec *timestamp);

/* Input stream specific APIs */

/* This method creates and opens the audio hardware input stream */
int qahw_open_input_stream(qahw_module_handle_t *hw_module,
                           audio_io_handle_t handle,
                           audio_devices_t devices,
                           struct audio_config *config,
                           qahw_stream_handle_t **stream_in,
                           audio_input_flags_t flags,
                           const char *address,
                           audio_source_t source);

int qahw_close_input_stream(qahw_stream_handle_t *in_handle);

/*
 * Return the sampling rate in Hz - eg. 44100.
 */
uint32_t qahw_in_get_sample_rate(const qahw_stream_handle_t *in_handle);

/*
 * currently unused - use set_parameters with key
 *    QAHW_PARAMETER_STREAM_SAMPLING_RATE
 */
int qahw_in_set_sample_rate(qahw_stream_handle_t *in_handle, uint32_t rate);

/*
 * Return size of input/output buffer in bytes for this stream - eg. 4800.
 * It should be a multiple of the frame size.  See also get_input_buffer_size.
 */
size_t qahw_in_get_buffer_size(const qahw_stream_handle_t *in_handle);

/*
 * Return the channel mask -
 *  e.g. AUDIO_CHANNEL_OUT_STEREO or AUDIO_CHANNEL_IN_STEREO
 */
audio_channel_mask_t qahw_in_get_channels(const qahw_stream_handle_t *in_handle);

/*
 * Return the audio format - e.g. AUDIO_FORMAT_PCM_16_BIT
 */
audio_format_t qahw_in_get_format(const qahw_stream_handle_t *in_handle);

/*
 * currently unused - use set_parameters with key
 *     QAHW_PARAMETER_STREAM_FORMAT
 */
int qahw_in_set_format(qahw_stream_handle_t *in_handle, audio_format_t format);

/*
 * Put the audio hardware input/output into standby mode.
 * Driver should exit from standby mode at the next I/O operation.
 * Returns 0 on success and <0 on failure.
 */
int qahw_in_standby(qahw_stream_handle_t *in_handle);

/*
 * set/get audio stream parameters. The function accepts a list of
 * parameter key value pairs in the form: key1=value1;key2=value2;...
 *
 * Some keys are reserved for standard parameters (See AudioParameter class)
 *
 * If the implementation does not accept a parameter change while
 * the output is active but the parameter is acceptable otherwise, it must
 * return -ENOSYS.
 *
 * The audio flinger will put the stream in standby and then change the
 * parameter value.
 */
int qahw_in_set_parameters(qahw_stream_handle_t *in_handle, const char *kv_pairs);

/*
 * Returns a pointer to a heap allocated string. The caller is responsible
 * for freeing the memory for it using free().
 */
char* qahw_in_get_parameters(const qahw_stream_handle_t *in_handle,
                              const char *keys);
/*
 * Read audio buffer in from audio driver. Returns number of bytes read, or a
 * negative status_t. meta_data structure is filled buffer pointer, start
 * offset and valid catpure timestamp (if session is opened with timetamp flag)
 * and buffer. if at least one frame was read prior to the error,
 * read should return that byte count and then return an error in the
 * subsequent call.
 */
ssize_t qahw_in_read(qahw_stream_handle_t *in_handle,
                     qahw_in_buffer_t *in_buf);
/*
 * Stop input stream. Returns zero on success.
 */
int qahw_in_stop(qahw_stream_handle_t *in_handle);
/*
 * Return the amount of input frames lost in the audio driver since the
 * last call of this function.
 * Audio driver is expected to reset the value to 0 and restart counting
 * upon returning the current value by this function call.
 * Such loss typically occurs when the user space process is blocked
 * longer than the capacity of audio driver buffers.
 *
 * Unit: the number of input audio frames
 */
uint32_t qahw_in_get_input_frames_lost(qahw_stream_handle_t *in_handle);

/*
 * Return a recent count of the number of audio frames received and
 * the clock time associated with that frame count.
 *
 * frames is the total frame count received. This should be as early in
 *     the capture pipeline as possible. In general,
 *     frames should be non-negative and should not go "backwards".
 *
 * time is the clock MONOTONIC time when frames was measured. In general,
 *     time should be a positive quantity and should not go "backwards".
 *
 * The status returned is 0 on success, -ENOSYS if the device is not
 * ready/available, or -EINVAL if the arguments are null or otherwise invalid.
 */
int qahw_in_get_capture_position(const qahw_stream_handle_t *in_handle,
                                 int64_t *frames, int64_t *time);

/* Module specific APIs */

/* convenience API for opening and closing an audio HAL module */
qahw_module_handle_t *qahw_load_module(const char *hw_module_id);

int qahw_unload_module(qahw_module_handle_t *hw_module);

/*
 * check to see if the audio hardware interface has been initialized.
 * returns 0 on success, -ENODEV on failure.
 */
int qahw_init_check(const qahw_module_handle_t *hw_module);

/* set the audio volume of a voice call. Range is between 0.0 and 1.0 */
int qahw_set_voice_volume(qahw_module_handle_t *hw_module, float volume);

/*
 * set_mode is called when the audio mode changes. AUDIO_MODE_NORMAL mode
 * is for standard audio playback, AUDIO_MODE_RINGTONE when a ringtone is
 * playing, and AUDIO_MODE_IN_CALL when a call is in progress.
 */
int qahw_set_mode(qahw_module_handle_t *hw_module, audio_mode_t mode);

/* Mute/unmute mic during voice/voip/HFP call */
int qahw_set_mic_mute(qahw_module_handle_t *hw_module, bool state);

/* Get mute/unmute status of mic during voice call */
int qahw_get_mic_mute(qahw_module_handle_t *hw_module, bool *state);

/* set/get global audio parameters */
int qahw_set_parameters(qahw_module_handle_t *hw_module, const char *kv_pairs);

/*
 * Returns a pointer to a heap allocated string. The caller is responsible
 * for freeing the memory for it using free().
 */
char* qahw_get_parameters(const qahw_module_handle_t *hw_module,
                           const char *keys);

/* Returns audio input buffer size according to parameters passed or
 * 0 if one of the parameters is not supported.
 * See also get_buffer_size which is for a particular stream.
 */
size_t qahw_get_input_buffer_size(const qahw_module_handle_t *hw_module,
                                  const struct audio_config *config);

/*returns current QTI HAL version */
int qahw_get_version();

/* Api to implement get parameters based on keyword param_id
 * and store data in payload.
 */
int qahw_get_param_data(const qahw_module_handle_t *hw_module,
                        qahw_param_id param_id,
                        qahw_param_payload *payload);

/* Api to implement set parameters based on keyword param_id
 * and data present in payload.
 */
int qahw_set_param_data(const qahw_module_handle_t *hw_module,
                        qahw_param_id param_id,
                        qahw_param_payload *payload);

/* Creates an audio patch between several source and sink ports.
 * The handle is allocated by the HAL and should be unique for this
 * audio HAL module.
 */
int qahw_create_audio_patch(qahw_module_handle_t *hw_module,
                        unsigned int num_sources,
                        const struct audio_port_config *sources,
                        unsigned int num_sinks,
                        const struct audio_port_config *sinks,
                        audio_patch_handle_t *handle);

/* Release an audio patch */
int qahw_release_audio_patch(qahw_module_handle_t *hw_module,
                        audio_patch_handle_t handle);

/* API to set loopback stream specific config parameters */
int qahw_loopback_set_param_data(qahw_module_handle_t *hw_module,
                                 audio_patch_handle_t handle,
                                 qahw_loopback_param_id param_id,
                                 qahw_loopback_param_payload *payload);

/* Fills the list of supported attributes for a given audio port.
 * As input, "port" contains the information (type, role, address etc...)
 * needed by the HAL to identify the port.
 * As output, "port" contains possible attributes (sampling rates, formats,
 * channel masks, gain controllers...) for this port.
 */
int qahw_get_audio_port(qahw_module_handle_t *hw_module,
                      struct audio_port *port);

/* Set audio port configuration */
int qahw_set_audio_port_config(qahw_module_handle_t *hw_module,
                     const struct audio_port_config *config);

void qahw_register_qas_death_notify_cb(audio_error_callback cb, void* context);

/* updated new stream APIs to support voice and Audio use cases */

int qahw_stream_open(qahw_module_handle_t *hw_module,
                     struct qahw_stream_attributes attr,
                     uint32_t num_of_devices,
                     qahw_device_t *devices,
                     uint32_t no_of_modifiers,
                     struct qahw_modifier_kv *modifiers,
                     qahw_stream_callback_t cb,
                     void *cookie,
                     qahw_stream_handle_t **stream_handle);

int qahw_stream_close(qahw_stream_handle_t *stream_handle);

int qahw_stream_start(qahw_stream_handle_t *stream_handle);

int qahw_stream_stop(qahw_stream_handle_t *stream_handle);

int qahw_stream_set_device(qahw_stream_handle_t *stream_handle,
                           uint32_t num_of_dev,
                           qahw_device_t *devices);

int qahw_stream_get_device(qahw_stream_handle_t *stream_handle,
                           uint32_t *num_of_dev,
                           qahw_device_t **devices);

int qahw_stream_set_volume(qahw_stream_handle_t *stream_handle,
                           struct qahw_volume_data vol_data);

int qahw_stream_get_volume(qahw_stream_handle_t *stream_handle,
                           struct qahw_volume_data **vol_data);

int qahw_stream_set_mute(qahw_stream_handle_t *stream_handle,
                         struct qahw_mute_data mute_data);

int qahw_stream_get_mute(qahw_stream_handle_t *stream_handle,
                         struct qahw_mute_data *mute_data);

ssize_t qahw_stream_read(qahw_stream_handle_t *stream_handle,
                         qahw_buffer_t *in_buf);

ssize_t qahw_stream_write(qahw_stream_handle_t *stream_handle,
                          qahw_buffer_t *out_buf);

int32_t qahw_stream_pause(qahw_stream_handle_t *stream_handle);

int32_t qahw_stream_standby(qahw_stream_handle_t *stream_handle);

int32_t qahw_stream_resume(qahw_stream_handle_t *stream_handle);

int32_t qahw_stream_flush(qahw_stream_handle_t *stream_handle);

int32_t qahw_stream_drain(qahw_stream_handle_t *stream_handle,
                          qahw_drain_type_t type);

int32_t qahw_stream_get_buffer_size(const qahw_stream_handle_t *stream_handle,
                                    size_t *in_buffer, size_t *out_buffer);

int32_t qahw_stream_set_buffer_size(const qahw_stream_handle_t *stream_handle,
                                    size_t in_buffer, size_t out_buffer);

int32_t qahw_stream_set_parameters(qahw_stream_handle_t *stream_handle,
                                   uint32_t param_id,
                                   qahw_param_payload *param_payload);

int32_t qahw_stream_get_parameters(qahw_stream_handle_t *stream_handle,
                                   uint32_t param_id,
                                   qahw_param_payload *param_payload);

__END_DECLS

#endif  // QTI_AUDIO_QAHW_API_H
