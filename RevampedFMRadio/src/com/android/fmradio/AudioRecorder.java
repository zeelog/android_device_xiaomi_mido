/*
 * Copyright (C) 2016 The CyanogenMod Project
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

package com.android.fmradio;

import android.media.AudioFormat;
import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.media.MediaMuxer;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;
import android.os.Message;
import android.util.Log;

import java.io.File;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.LinkedList;
import java.util.concurrent.Semaphore;

class AudioRecorder extends HandlerThread implements Handler.Callback {
    public static final int AUDIO_RECORDER_ERROR_INTERNAL = -100;
    public static final int AUDIO_RECORDER_WARN_DISK_LOW = 100;
    private static final boolean TRACE = false;
    private static final String TAG = "AudioRecorder";
    private static final int MSG_INIT = 100;
    private static final int MSG_ENCODE = 101;
    private static final int MSG_STOP = 999;
    private static final long DISK_LOW_THRESHOLD = 10 * 1024 * 1024;
    private AudioFormat mInputFormat;
    private Handler mHandler;
    private File mFilePath;
    private MediaMuxer mMuxer;
    private MediaCodec mCodec;
    private MediaFormat mRequestedFormat;
    private LinkedList<Sample> mQueue = new LinkedList<>();
    private MediaFormat mOutFormat;
    private int mMuxerTrack;
    private float mRate; // bytes per us
    private long mInputBufferPosition;
    private int mInputBufferIndex = -1;
    /** This semaphore is initialized when stopRecording() is called and blocks
        until recording is stopped. */
    private Semaphore mFinalSem;
    private boolean mFinished;
    private Handler mCallbackHandler;
    private Callback mCallback;

    AudioRecorder(AudioFormat format, File filePath) {
        super("AudioRecorder Thread");
        mFilePath = filePath;
        mInputFormat = format;

        start();

        mHandler = new Handler(getLooper(), this);
        mHandler.obtainMessage(MSG_INIT).sendToTarget();
    }

    public void setCallback(Callback callback) {
        mCallback = callback;
        mCallbackHandler = new Handler(Looper.getMainLooper());
    }

    /**
     * Encode bytes of audio to file
     *
     * @param bytes - PCM inbut buffer
     */
    public void encode(byte[] bytes) {
        if (mFinished) {
            Log.w(TAG, "encode() called after stopped");
            return;
        }
        Sample s = new Sample();
        s.bytes = bytes;
        mHandler.obtainMessage(MSG_ENCODE, s).sendToTarget();
    }

    /**
     * Stop the current recording.
     * Blocks until the recording finishes cleanly.
     */
    public void stopRecording() {
        if (mFinished) {
            Log.w(TAG, "stopRecording() called after stopped");
            return;
        }

        mFinished = true;
        Log.d(TAG, "Stopping");
        Semaphore done = new Semaphore(0);
        mHandler.obtainMessage(MSG_STOP, done).sendToTarget();

        try {
            // block until done
            done.acquire();
        } catch (InterruptedException ex) {
            Log.e(TAG, "interrupted waiting for encoding to finish", ex);
        } finally {
            quitSafely();
        }
    }

    private void init() {
        Log.i(TAG, "Starting AudioRecorder with format=" + mInputFormat + ". Saving to: " + mFilePath);
        calculateInputRate();

        mRequestedFormat = new MediaFormat();
        mRequestedFormat.setString(MediaFormat.KEY_MIME, "audio/mp4a-latm");
        mRequestedFormat.setInteger(MediaFormat.KEY_BIT_RATE, 128000);
        mRequestedFormat.setInteger(MediaFormat.KEY_CHANNEL_COUNT, mInputFormat.getChannelCount());
        mRequestedFormat.setInteger(MediaFormat.KEY_SAMPLE_RATE, mInputFormat.getSampleRate());
        mRequestedFormat.setInteger(MediaFormat.KEY_AAC_PROFILE, MediaCodecInfo.CodecProfileLevel.AACObjectLC);

        try {
            mCodec = MediaCodec.createEncoderByType("audio/mp4a-latm");
        } catch (IOException ex) {
            onError("failed creating encoder", ex);
            return;
        }
        mCodec.setCallback(new AudioRecorderCodecCallback(), new Handler(getLooper()));
        mCodec.configure(mRequestedFormat, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
        mCodec.start();

        try {
            mMuxer = new MediaMuxer(mFilePath.getAbsolutePath(), MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4);
        } catch (IOException ex) {
            onError("failed creating muxer", ex);
            return;
        }

        mOutFormat = mCodec.getOutputFormat();
        mMuxerTrack = mMuxer.addTrack(mOutFormat);
        mMuxer.start();
    }

    @Override
    public boolean handleMessage(Message msg) {
        if (msg.what == MSG_INIT) {
            init();
        } else if (msg.what == MSG_STOP) {
            mFinalSem = (Semaphore) msg.obj;
            if (mInputBufferIndex >= 0) {
                processInputBuffer();
            }
        } else if (msg.what == MSG_ENCODE) {
            mQueue.addLast((Sample) msg.obj);
            if (mInputBufferIndex >= 0) {
                processInputBuffer();
            }
        }
        return true;
    }

    private void processInputBuffer() {
        Sample s = mQueue.peekFirst();
        if (s == null) { // input available?
            if (mFinalSem != null) {
                // input queue is exhausted and stopRecording() is waiting for
                // encoding to finish. signal end-of-stream on the input.
                Log.d(TAG, "Input EOS");
                mCodec.queueInputBuffer(
                        mInputBufferIndex, 0, 0,
                        getPresentationTimestampUs(mInputBufferPosition),
                        MediaCodec.BUFFER_FLAG_END_OF_STREAM);
            }
            return;
        }

        ByteBuffer b = mCodec.getInputBuffer(mInputBufferIndex);
        assert b != null;
        int sz = Math.min(b.capacity(), s.bytes.length - s.offset);
        long ts = getPresentationTimestampUs(mInputBufferPosition);
        if (TRACE)
            Log.v(TAG, String.format("processInputBuffer (len=%d) ts=%.3f", sz, ts * 1e-6));

        b.put(s.bytes, s.offset, sz);
        mCodec.queueInputBuffer(mInputBufferIndex, 0, sz, ts, 0);

        mInputBufferPosition += sz;
        s.offset += sz;

        // done with this sample?
        if (s.offset >= s.bytes.length) {
            mQueue.pop();
        }

        // done with this buffer
        mInputBufferIndex = -1;
    }

    private void processOutputBuffer(int index, MediaCodec.BufferInfo info) {
        ByteBuffer outputBuffer = mCodec.getOutputBuffer(index);
        assert outputBuffer != null;

        if (TRACE)
            Log.v(TAG, String.format("processOutputBuffer (len=%d) ts=%.3f",
                    outputBuffer.limit(), info.presentationTimeUs * 1e-6));

        mMuxer.writeSampleData(mMuxerTrack, outputBuffer, info);
        mCodec.releaseOutputBuffer(index, false);
        if ((info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) {
            Log.d(TAG, "Output EOS");
            finish();
        } else if (mFilePath.getFreeSpace() < DISK_LOW_THRESHOLD) {
            onDiskLow();
        }
    }

    private void onDiskLow() {
        mCallbackHandler.post(new Runnable() {
            @Override
            public void run() {
                if (mCallback != null) {
                    mCallback.onError(AUDIO_RECORDER_WARN_DISK_LOW);
                }
            }
        });
    }

    private void onError(String s, Exception e) {
        Log.e(TAG, s, e);
        mFinished = true;
        stopAndRelease();
        mCallbackHandler.post(new Runnable() {
            @Override
            public void run() {
                quitSafely();
                if (mCallback != null) {
                    mCallback.onError(AUDIO_RECORDER_ERROR_INTERNAL);
                }
            }
        });
    }

    private void finish() {
        assert mFinalSem != null;
        stopAndRelease();
        mFinalSem.release();
    }

    private void stopAndRelease() {
        // can fail early on before codec/muxer are created
        if (mCodec != null) {
            mCodec.stop();
            mCodec.release();
        }

        if (mMuxer != null) {
            mMuxer.stop();
            mMuxer.release();
        }
    }

    private void calculateInputRate() {
        int bits_per_sample;
        switch (mInputFormat.getEncoding()) {
            case AudioFormat.ENCODING_PCM_8BIT:
                bits_per_sample = 8;
                break;
            case AudioFormat.ENCODING_PCM_16BIT:
                bits_per_sample = 16;
                break;
            case AudioFormat.ENCODING_PCM_FLOAT:
                bits_per_sample = 32;
                break;
            default:
                throw new IllegalArgumentException("Unexpected encoding: " + mInputFormat.getEncoding());
        }

        mRate = bits_per_sample;
        mRate *= mInputFormat.getSampleRate();
        mRate *= mInputFormat.getChannelCount();
        mRate *= 1e-6; // -> us
        mRate /= 8; // -> bytes

        Log.v(TAG, "Rate: " + mRate);
    }

    private long getPresentationTimestampUs(long position) {
        return (long) (position / mRate);
    }

    public interface Callback {
        void onError(int what);
    }

    class AudioRecorderCodecCallback extends MediaCodec.Callback {

        @Override
        public void onInputBufferAvailable(MediaCodec codec, int index) {
            mInputBufferIndex = index;
            processInputBuffer();
        }

        @Override
        public void onOutputBufferAvailable(MediaCodec codec, int index, MediaCodec.BufferInfo info) {
            processOutputBuffer(index, info);
        }

        @Override
        public void onError(MediaCodec codec, MediaCodec.CodecException e) {
            AudioRecorder.this.onError("Encoder error", e);
        }

        @Override
        public void onOutputFormatChanged(MediaCodec codec, MediaFormat format) {
        }
    }

    private class Sample {
        byte bytes[];
        int offset;
    }
}
