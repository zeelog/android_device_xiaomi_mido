/*
 * Copyright (C) 2014,2022 The Android Open Source Project
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

import android.app.ActivityManager;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothProfile;
import android.content.BroadcastReceiver;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.res.Configuration;
import android.database.Cursor;
import android.graphics.Bitmap;
import android.media.AudioAttributes;
import android.media.AudioDevicePort;
import android.media.AudioDevicePortConfig;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioManager.OnAudioFocusChangeListener;
import android.media.AudioManager.OnAudioPortUpdateListener;
import android.media.AudioMixPort;
import android.media.AudioPatch;
import android.media.AudioPort;
import android.media.AudioPortConfig;
import android.media.AudioRecord;
import android.media.AudioSystem;
import android.media.AudioTrack;
import android.media.MediaMetadata;
import android.media.MediaRecorder;
import android.media.session.MediaSession;
import android.media.session.PlaybackState;
import android.net.Uri;
import android.os.Binder;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.Looper;
import android.os.Message;
import android.os.PowerManager;
import android.os.PowerManager.WakeLock;
import android.os.Process;
import android.text.TextUtils;
import android.util.Log;
import android.view.KeyEvent;

import com.android.fmradio.FmStation.Station;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Iterator;

/**
 * Background service to control FM or do background tasks.
 */
public class FmService extends Service implements FmRecorder.OnRecorderStateChangedListener {
    // Logging
    private static final String TAG = "FmService";

    // Broadcast messages from other sounder APP to FM service
    private static final String SOUND_POWER_DOWN_MSG = "com.android.music.musicservicecommand";
    private static final String FM_SEEK_PREVIOUS = "fmradio.seek.previous";
    private static final String FM_SEEK_NEXT = "fmradio.seek.next";
    private static final String FM_TURN_ON = "fmradio.turnon";
    private static final String FM_TURN_OFF = "fmradio.turnoff";
    private static final String CMDPAUSE = "pause";

    // HandlerThread Keys
    private static final String FM_FREQUENCY = "frequency";
    private static final String OPTION = "option";
    private static final String RECODING_FILE_NAME = "name";

    // RDS events
    // PS
    private static final int RDS_EVENT_PROGRAMNAME = 0x0008;
    // RT
    private static final int RDS_EVENT_LAST_RADIOTEXT = 0x0040;
    // AF
    private static final int RDS_EVENT_AF = 0x0080;

    // Headset
    private static final int HEADSET_PLUG_IN = 1;

    // Notification id
    private static final int NOTIFICATION_ID = 1;

    // Notification channel
    public static final String NOTIFICATION_CHANNEL = "fmradio_notification_channel";

    // ignore audio data
    private static final int AUDIO_FRAMES_TO_IGNORE_COUNT = 3;

    // Set audio policy for FM
    // should check AUDIO_POLICY_FORCE_FOR_MEDIA in audio_policy.h
    private static final int FOR_PROPRIETARY = 1;
    // Forced Use value
    private int mForcedUseForMedia;

    // FM recorder
    FmRecorder mFmRecorder = null;
    private BroadcastReceiver mSdcardListener = null;
    private int mRecordState = FmRecorder.STATE_INVALID;
    private int mRecorderErrorType = -1;
    // If eject record sdcard, should set Value false to not record.
    // Key is sdcard path(like "/storage/sdcard0"), V is to enable record or
    // not.
    private HashMap<String, Boolean> mSdcardStateMap = new HashMap<String, Boolean>();
    // The show name in save dialog but saved in service
    // If modify the save title it will be not null, otherwise it will be null
    private String mModifiedRecordingName = null;
    // record the listener list, will notify all listener in list
    private ArrayList<Record> mRecords = new ArrayList<Record>();
    // record FM whether in recording mode
    private boolean mIsInRecordingMode = false;
    // record sd card path when start recording
    private static String sRecordingSdcard = FmUtils.getDefaultStoragePath();

    // RDS
    // PS String
    private String mPsString = "";
    // RT String
    private String mRtTextString = "";
    // Notification target class name
    private String mTargetClassName = "com.android.fmradio.FmMainActivity";
    // RDS thread use to receive the information send by station
    private Thread mRdsThread = null;
    // record whether RDS thread exit
    private boolean mIsRdsThreadExit = false;

    // State variables
    // Record whether FM is in native scan state
    private boolean mIsNativeScanning = false;
    // Record whether FM is in scan thread
    private boolean mIsScanning = false;
    // Record whether FM is in seeking state
    private boolean mIsNativeSeeking = false;
    // Record whether FM is in native seek
    private boolean mIsSeeking = false;
    // Record whether searching progress is canceled
    private boolean mIsStopScanCalled = false;
    // Record whether is speaker used
    private boolean mIsSpeakerUsed = true;
    // Record whether device is open
    private boolean mIsDeviceOpen = false;
    // Record Power Status
    private int mPowerStatus = POWER_DOWN;

    // Notification manager
    private static Object mNotificationLock = new Object();
    private NotificationManager mNotificationManager = null;
    private NotificationChannel mNotificationChannel = null;
    private MediaSession mSession;
    private String mCachedArtworkKey = null;
    private Bitmap mCachedArtwork = null;

    public static int POWER_UP = 0;
    public static int DURING_POWER_UP = 1;
    public static int POWER_DOWN = 2;
    // Record whether service is init
    private boolean mIsServiceInited = false;
    // Fm power down by loss audio focus,should make power down menu item can
    // click
    private boolean mIsPowerDown = false;
    // distance is over 100 miles(160934.4m)
    private boolean mIsDistanceExceed = false;
    // FmMainActivity foreground
    private boolean mIsFmMainForeground = true;
    // FmFavoriteActivity foreground
    private boolean mIsFmFavoriteForeground = false;
    // FmRecordActivity foreground
    private boolean mIsFmRecordForeground = false;
    // Instance variables
    private Context mContext = null;
    private AudioManager mAudioManager = null;
    private ActivityManager mActivityManager = null;
    //private MediaPlayer mFmPlayer = null;
    private WakeLock mWakeLock = null;
    // Audio focus is held or not
    private boolean mIsAudioFocusHeld = false;
    // Focus transient lost
    private boolean mPausedByTransientLossOfFocus = false;
    private int mCurrentStation = FmUtils.DEFAULT_STATION;
    // Headset plug state (0:long antenna plug in, 1:long antenna plug out)
    private int mValueHeadSetPlug = 1;
    // For bind service
    private final IBinder mBinder = new ServiceBinder();
    // Broadcast to receive the external event
    private FmServiceBroadcastReceiver mBroadcastReceiver = null;
    // Async handler
    private FmRadioServiceHandler mFmServiceHandler;
    // Lock for lose audio focus and receive SOUND_POWER_DOWN_MSG
    // at the same time
    // while recording call stop recording not finished(status is still
    // RECORDING), but
    // SOUND_POWER_DOWN_MSG will exitFm(), if it is RECORDING will discard the
    // record.
    // 1. lose audio focus -> stop recording(lock) -> set to IDLE and show save
    // dialog
    // 2. exitFm() -> check the record status, discard it if it is recording
    // status(lock)
    // Add this lock the exitFm() while stopRecording()
    private Object mStopRecordingLock = new Object();
    // The listener for exit, should finish favorite when exit FM
    private static OnExitListener sExitListener = null;
    // The latest status for mute/unmute
    private boolean mIsMuted = false;

    // Audio Patch
    private AudioPatch mAudioPatch = null;
    private Object mRenderLock = new Object();

    @Override
    public IBinder onBind(Intent intent) {
        return mBinder;
    }

    /**
     * class use to return service instance
     */
    public class ServiceBinder extends Binder {
        /**
         * get FM service instance
         *
         * @return service instance
         */
        FmService getService() {
            return FmService.this;
        }
    }

    /**
     * Broadcast monitor external event, Other app want FM stop, Phone shut
     * down, screen state, headset state
     */
    private class FmServiceBroadcastReceiver extends BroadcastReceiver {

        @Override
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            String command = intent.getStringExtra("command");
            Log.d(TAG, "onReceive, action = " + action + " / command = " + command);
            // other app want FM stop, stop FM

            if (CMDPAUSE.equals(command)) {
                // need remove all messages, make power down will be execute
                mFmServiceHandler.removeCallbacksAndMessages(null);
                Log.d(TAG, "Stopping FM playback");
                powerDownAsync();
            } else if (SOUND_POWER_DOWN_MSG.equals(action)) {
                // phone shut down, so exit FM
                // need remove all messages, make power down will be execute
                mFmServiceHandler.removeCallbacksAndMessages(null);
                exitFm();
                stopSelf();
                // phone shut down, so exit FM
            } else if (Intent.ACTION_SHUTDOWN.equals(action)) {
                /**
                 * here exitFm, system will send broadcast, system will shut
                 * down, so fm does not need call back to activity
                 */
                mFmServiceHandler.removeCallbacksAndMessages(null);
                exitFm();
                // screen on, if FM play, open rds
            } else if (Intent.ACTION_SCREEN_ON.equals(action)) {
                FmNative.setNormalPowerMode();
                setRdsAsync(true);
                // screen off, if FM play, close rds
            } else if (Intent.ACTION_SCREEN_OFF.equals(action)) {
                setRdsAsync(false);
                FmNative.setLowPowerMode();
                // switch antenna when headset plug in or plug out
            } else if (Intent.ACTION_HEADSET_PLUG.equals(action)) {
                // switch antenna should not impact audio focus status
                mValueHeadSetPlug = (intent.getIntExtra("state", -1) == HEADSET_PLUG_IN) ? 0 : 1;

                mIsSpeakerUsed = !isHeadSetIn();

                // Avoid Service is killed,and receive headset plug in
                // broadcast again
                if (!mIsServiceInited) {
                    Log.d(TAG, "onReceive, mIsServiceInited is false");
                    switchAntennaAsync(mValueHeadSetPlug);
                    return;
                }
                /*
                 * If ear phone insert and activity is
                 * foreground. power up FM automatic
                 */
                if (isHeadSetIn() && isActivityForeground()) {
                    powerUpAsync(FmUtils.computeFrequency(mCurrentStation));
                }

                // Notify UI
                Bundle bundle = new Bundle(2);
                bundle.putInt(FmListener.CALLBACK_FLAG,
                        FmListener.LISTEN_SPEAKER_MODE_CHANGED);
                bundle.putBoolean(FmListener.KEY_IS_SPEAKER_MODE, !isHeadSetIn());
                notifyActivityStateChanged(bundle);

                switchAntennaAsync(mValueHeadSetPlug);
            }
        }
    }

    /**
     * Handle sdcard mount/unmount event. 1. Update the sdcard state map 2. If
     * the recording sdcard is unmounted, need to stop and notify
     */
    private class SdcardListener extends BroadcastReceiver {
        @Override
        public void onReceive(Context context, Intent intent) {
            // If eject record sdcard, should set this false to not
            // record.
            updateSdcardStateMap(intent);

            if (mFmRecorder == null) {
                Log.w(TAG, "SdcardListener.onReceive, mFmRecorder is null");
                return;
            }

            String action = intent.getAction();
            if (Intent.ACTION_MEDIA_EJECT.equals(action) ||
                    Intent.ACTION_MEDIA_UNMOUNTED.equals(action)) {
                // If not unmount recording sd card, do nothing;
                if (isRecordingCardUnmount(intent)) {
                    if (mFmRecorder.getState() == FmRecorder.STATE_RECORDING) {
                        onRecorderError(FmRecorder.ERROR_SDCARD_NOT_PRESENT);
                        mFmRecorder.discardRecording();
                    } else {
                        Bundle bundle = new Bundle(2);
                        bundle.putInt(FmListener.CALLBACK_FLAG,
                                FmListener.LISTEN_RECORDSTATE_CHANGED);
                        bundle.putInt(FmListener.KEY_RECORDING_STATE,
                                FmRecorder.STATE_IDLE);
                        notifyActivityStateChanged(bundle);
                    }
                }
                return;
            }
        }
    }

    /**
     * whether antenna available
     *
     * @return true, antenna available; false, antenna not available
     */
    public boolean isAntennaAvailable() {
        return true; // force wireless
    }

    private void setForceUse(boolean isSpeaker) {
        mForcedUseForMedia = isSpeaker ? AudioSystem.FORCE_SPEAKER : AudioSystem.FORCE_NONE;
        AudioSystem.setForceUse(FOR_PROPRIETARY, mForcedUseForMedia);
        mIsSpeakerUsed = isSpeaker;
    }

    /**
     * Set FM audio from speaker or not
     *
     * @param isSpeaker true if set FM audio from speaker
     */
    public void setSpeakerPhoneOn(boolean isSpeaker) {
        Log.d(TAG, "setSpeakerPhoneOn " + isSpeaker);
        setForceUse(isSpeaker);
    }

    /**
     * Check if BT headset is connected
     * @return true if current is playing with BT headset
     */
    public boolean isBluetoothHeadsetInUse() {
        BluetoothAdapter btAdapter = BluetoothAdapter.getDefaultAdapter();
        int a2dpState = btAdapter.getProfileConnectionState(BluetoothProfile.HEADSET);
        return (BluetoothProfile.STATE_CONNECTED == a2dpState
                || BluetoothProfile.STATE_CONNECTING == a2dpState);
    }

    private synchronized void startRender() {
        Log.d(TAG, "startRender " + AudioSystem.getForceUse(FOR_PROPRIETARY));

        exitRenderThread();

       // need to create new audio record and audio play back track,
       // because input/output device may be changed.
       if (mAudioRecord != null) {
           mAudioRecord.stop();
           mAudioRecord.release();
           mAudioRecord = null;
       }
       if (mAudioTrack != null) {
           mAudioTrack.stop();
           mAudioTrack.release();
           mAudioTrack = null;
       }
       initAudioRecordSink();

        mIsRender = true;
        createRenderThread();
        synchronized (mRenderLock) {
            mRenderLock.notify();
        }
    }

    private synchronized void stopRender() {
        Log.d(TAG, "stopRender");
        mIsRender = false;
        // HACK: Set volume to 0 to squelch any output between the call to
        // stopRender and the render thread calling AudioTrack.stop
        mAudioTrack.setVolume(0.0f);
    }

    private synchronized void createRenderThread() {
        if (mRenderThread == null) {
            mRenderThread = new RenderThread();
            mRenderThread.start();
        }
    }

    private synchronized void exitRenderThread() {
        mRenderThread.interrupt();
        try {
            mRenderThread.join();
        } catch (InterruptedException ie) {
            Log.e(TAG, "Failed to join render thread");
        }
        mRenderThread = null;
    }

    private Thread mRenderThread = null;
    private AudioRecord mAudioRecord = null;
    private AudioTrack mAudioTrack = null;
    private static final int SAMPLE_RATE = 44100;
    private static final int CHANNEL_CONFIG = AudioFormat.CHANNEL_CONFIGURATION_STEREO;
    private static final int AUDIO_FORMAT = AudioFormat.ENCODING_PCM_16BIT;
    private static final int RECORD_BUF_SIZE = AudioRecord.getMinBufferSize(SAMPLE_RATE,
            CHANNEL_CONFIG, AUDIO_FORMAT);
    private boolean mIsRender = false;

    AudioDevicePort mAudioSource = null;
    AudioDevicePort mAudioSink = null;

    private boolean isRendering() {
        return mIsRender;
    }

    private void startAudioTrack() {
        if (mAudioTrack.getPlayState() == AudioTrack.PLAYSTATE_STOPPED) {
            ArrayList<AudioPatch> patches = new ArrayList<AudioPatch>();
            mAudioManager.listAudioPatches(patches);
            mAudioTrack.play();
        }
    }

    private void stopAudioTrack() {
        if (mAudioTrack.getPlayState() == AudioTrack.PLAYSTATE_PLAYING) {
            mAudioTrack.stop();
        }
    }

    class RenderThread extends Thread {
        private int mCurrentFrame = 0;
        private boolean isAudioFrameNeedIgnore() {
            return mCurrentFrame < AUDIO_FRAMES_TO_IGNORE_COUNT;
        }

        @Override
        public void run() {
            Process.setThreadPriority(Process.THREAD_PRIORITY_AUDIO);
            try {
                byte[] buffer = new byte[RECORD_BUF_SIZE];
                while (!Thread.interrupted()) {
                    if (isRender()) {
                        // Speaker mode or BT a2dp mode will come here and keep reading and writing.
                        // If we want FM sound output from speaker or BT a2dp, we must record data
                        // to AudioRecrd and write data to AudioTrack.
                        if (mAudioRecord.getRecordingState() == AudioRecord.RECORDSTATE_STOPPED) {
                            mAudioRecord.startRecording();
                        }

                        if (mAudioTrack.getPlayState() == AudioTrack.PLAYSTATE_STOPPED) {
                            mAudioTrack.play();
                        }
                        int size = mAudioRecord.read(buffer, 0, RECORD_BUF_SIZE);
                        // check whether need to ignore first 3 frames audio data from AudioRecord
                        // to avoid pop noise.
                        if (isAudioFrameNeedIgnore()) {
                            mCurrentFrame += 1;
                            continue ;
                        }
                        if (size <= 0) {
                            Log.e(TAG, "RenderThread read data from AudioRecord "
                                    + "error size: " + size);
                            continue;
                        }
                        byte[] tmpBuf = new byte[size];
                        System.arraycopy(buffer, 0, tmpBuf, 0, size);
                        // Check again to avoid noises, because mIsRender may be changed
                        // while AudioRecord is reading.
                        if (isRender()) {
                            mAudioTrack.write(tmpBuf, 0, tmpBuf.length);
                        }

                        if (mFmRecorder != null) {
                            mFmRecorder.encode(tmpBuf);
                        }
                    } else {
                        // Earphone mode will come here and wait.
                        mCurrentFrame = 0;

                        if (mAudioTrack.getPlayState() == AudioTrack.PLAYSTATE_PLAYING) {
                            mAudioTrack.pause();
                            mAudioTrack.flush();
                            mAudioTrack.stop();
                        }

                        if (mAudioRecord.getRecordingState() == AudioRecord.RECORDSTATE_RECORDING) {
                            mAudioRecord.stop();
                        }

                        synchronized (mRenderLock) {
                            mRenderLock.wait();
                        }
                    }
                }
            } catch (InterruptedException e) {
                Log.d(TAG, "RenderThread.run, thread is interrupted, need exit thread");
            } finally {
                if (mAudioRecord.getRecordingState() == AudioRecord.RECORDSTATE_RECORDING) {
                    mAudioRecord.stop();
                }
                if (mAudioTrack.getPlayState() == AudioTrack.PLAYSTATE_PLAYING) {
                    mAudioTrack.stop();
                }
            }
        }
    }

    // A2dp or speaker mode should render
    private boolean isRender() {
        return (mIsRender && isPlaying() && mIsAudioFocusHeld);
    }

    private boolean isSpeakerPhoneOn() {
        return (mForcedUseForMedia == AudioSystem.FORCE_SPEAKER);
    }

    /**
     * open FM device, should be call before power up
     *
     * @return true if FM device open, false FM device not open
     */
    private boolean openDevice() {
        if (!mIsDeviceOpen) {
            mIsDeviceOpen = FmNative.openDev();
        }
        return mIsDeviceOpen;
    }

    /**
     * close FM device
     *
     * @return true if close FM device success, false close FM device failed
     */
    private boolean closeDevice() {
        boolean isDeviceClose = false;
        if (mIsDeviceOpen) {
            isDeviceClose = FmNative.closeDev();
            mIsDeviceOpen = !isDeviceClose;
        }
        // quit looper
        mFmServiceHandler.getLooper().quit();
        return isDeviceClose;
    }

    /**
     * get FM device opened or not
     *
     * @return true FM device opened, false FM device closed
     */
    public boolean isDeviceOpen() {
        return mIsDeviceOpen;
    }

    /**
     * power up FM, and make FM voice output from earphone
     *
     * @param frequency
     */
    public void powerUpAsync(float frequency) {
        final int bundleSize = 1;
        mFmServiceHandler.removeMessages(FmListener.MSGID_POWERUP_FINISHED);
        mFmServiceHandler.removeMessages(FmListener.MSGID_POWERDOWN_FINISHED);
        Bundle bundle = new Bundle(bundleSize);
        bundle.putFloat(FM_FREQUENCY, frequency);
        Message msg = mFmServiceHandler.obtainMessage(FmListener.MSGID_POWERUP_FINISHED);
        msg.setData(bundle);
        mFmServiceHandler.sendMessage(msg);
    }

    private boolean powerUp(float frequency) {
        if (isPlaying()) {
            return true;
        }
        if (!mWakeLock.isHeld()) {
            mWakeLock.acquire();
        }
        if (!requestAudioFocus()) {
            // activity used for update powerdown menu
            mPowerStatus = POWER_DOWN;
            return false;
        }

        mPowerStatus = DURING_POWER_UP;

        // if device open fail when chip reset, it need open device again before
        // power up
        if (!mIsDeviceOpen) {
            openDevice();
        }

        if (!FmNative.powerUp(frequency)) {
            mPowerStatus = POWER_DOWN;
            return false;
        }
        mPowerStatus = POWER_UP;
        // need mute after power up
        setMute(true);

        return isPlaying();
    }

    private boolean playFrequency(float frequency) {
        mCurrentStation = FmUtils.computeStation(frequency);
        FmStation.setCurrentStation(mContext, mCurrentStation);
        // Add notification to the title bar.
        updatePlayingNotification();

        // Start the RDS thread if RDS is supported.
        if (isRdsSupported()) {
            startRdsThread();
        }

        if (!mWakeLock.isHeld()) {
            mWakeLock.acquire();
        }
        if (mIsSpeakerUsed != isSpeakerPhoneOn()) {
            setForceUse(mIsSpeakerUsed);
        }
        if (mRecordState != FmRecorder.STATE_PLAYBACK) {
            enableFmAudio(true);
        }

        setRds(true);
        setMute(false);

        return isPlaying();
    }

    /**
     * power down FM
     */
    public void powerDownAsync() {
        // if power down Fm, should remove message first.
        // not remove all messages, because such as recorder message need
        // to execute after or before power down
        mFmServiceHandler.removeMessages(FmListener.MSGID_SCAN_FINISHED);
        mFmServiceHandler.removeMessages(FmListener.MSGID_SEEK_FINISHED);
        mFmServiceHandler.removeMessages(FmListener.MSGID_TUNE_FINISHED);
        mFmServiceHandler.removeMessages(FmListener.MSGID_POWERDOWN_FINISHED);
        mFmServiceHandler.removeMessages(FmListener.MSGID_POWERUP_FINISHED);
        mFmServiceHandler.sendEmptyMessage(FmListener.MSGID_POWERDOWN_FINISHED);
    }

    /**
     * Power down FM
     *
     * @return true if power down success
     */
    private boolean powerDown() {
        if (mPowerStatus == POWER_DOWN) {
            return true;
        }

        if (mFmRecorder != null) {
            stopRecording();
        }

        setMute(true);
        setRds(false);
        enableFmAudio(false);

        if (!FmNative.powerDown(0)) {

            if (isRdsSupported()) {
                stopRdsThread();
            }

            if (mWakeLock.isHeld()) {
                mWakeLock.release();
            }
            // Update the notification
            showPlayingNotification();
            return false;
        }
        // activity used for update powerdown menu
        mPowerStatus = POWER_DOWN;

        if (isRdsSupported()) {
            stopRdsThread();
        }

        if (mWakeLock.isHeld()) {
            mWakeLock.release();
        }

        // Update the notification
        showPlayingNotification();
        return true;
    }

    public int getPowerStatus() {
        return mPowerStatus;
    }

    /**
     * Tune to a station
     *
     * @param frequency The frequency to tune
     *
     * @return true, success; false, fail.
     */
    public void tuneStationAsync(float frequency) {
        mFmServiceHandler.removeMessages(FmListener.MSGID_TUNE_FINISHED);
        final int bundleSize = 1;
        Bundle bundle = new Bundle(bundleSize);
        bundle.putFloat(FM_FREQUENCY, frequency);
        Message msg = mFmServiceHandler.obtainMessage(FmListener.MSGID_TUNE_FINISHED);
        msg.setData(bundle);
        mFmServiceHandler.sendMessage(msg);
    }

    private boolean tuneStation(float frequency) {
        if (isPlaying()) {
            setRds(false);
            boolean bRet = FmNative.tune(frequency);
            if (bRet) {
                setRds(true);
                mCurrentStation = FmUtils.computeStation(frequency);
                FmStation.setCurrentStation(mContext, mCurrentStation);
                updatePlayingNotification();
            }
            setMute(false);
            return bRet;
        }

        // if earphone is not insert, not power up
        if (!isAntennaAvailable()) {
            return false;
        }

        // if not power up yet, should powerup first
        boolean tune = false;

        if (powerUp(frequency)) {
            tune = playFrequency(frequency);
        }

        return tune;
    }

    /**
     * Seek station according frequency and direction
     *
     * @param frequency start frequency(100KHZ, 87.5)
     * @param isUp direction(true, next station; false, previous station)
     *
     * @return the frequency after seek
     */
    public void seekStationAsync(float frequency, boolean isUp) {
        mFmServiceHandler.removeMessages(FmListener.MSGID_SEEK_FINISHED);
        final int bundleSize = 2;
        Bundle bundle = new Bundle(bundleSize);
        bundle.putFloat(FM_FREQUENCY, frequency);
        bundle.putBoolean(OPTION, isUp);
        Message msg = mFmServiceHandler.obtainMessage(FmListener.MSGID_SEEK_FINISHED);
        msg.setData(bundle);
        mFmServiceHandler.sendMessage(msg);
    }

    private float seekStation(float frequency, boolean isUp) {
        if (mPowerStatus != POWER_UP) {
            return -1;
        }

        setRds(false);
        mIsNativeSeeking = true;
        float fRet = FmNative.seek(frequency, isUp);
        mIsNativeSeeking = false;
        // make mIsStopScanCalled false, avoid stop scan make this true,
        // when start scan, it will return null.
        mIsStopScanCalled = false;
        return fRet;
    }

    /**
     * Scan stations
     */
    public void startScanAsync() {
        mFmServiceHandler.removeMessages(FmListener.MSGID_SCAN_FINISHED);
        mFmServiceHandler.sendEmptyMessage(FmListener.MSGID_SCAN_FINISHED);
    }

    private int[] startScan() {
        int[] stations = null;

        setRds(false);
        setMute(true);
        short[] stationsInShort = null;
        if (!mIsStopScanCalled) {
            mIsNativeScanning = true;
            stationsInShort = FmNative.autoScan();
            mIsNativeScanning = false;
        }

        setRds(true);
        if (mIsStopScanCalled) {
            // Received a message to power down FM, or interrupted by a phone
            // call. Do not return any stations. stationsInShort = null;
            // if cancel scan, return invalid station -100
            stationsInShort = new short[] {
                -100
            };
            mIsStopScanCalled = false;
        }

        if (null != stationsInShort) {
            int size = stationsInShort.length;
            stations = new int[size];
            for (int i = 0; i < size; i++) {
                stations[i] = stationsInShort[i];
            }
        }
        return stations;
    }

    /**
     * Check FM Radio is in scan progress or not
     *
     * @return if in scan progress return true, otherwise return false.
     */
    public boolean isScanning() {
        return mIsScanning;
    }

    /**
     * Stop scan progress
     *
     * @return true if can stop scan, otherwise return false.
     */
    public boolean stopScan() {
        if (mPowerStatus != POWER_UP) {
            return false;
        }

        boolean bRet = false;
        mFmServiceHandler.removeMessages(FmListener.MSGID_SCAN_FINISHED);
        mFmServiceHandler.removeMessages(FmListener.MSGID_SEEK_FINISHED);
        if (mIsNativeScanning || mIsNativeSeeking) {
            mIsStopScanCalled = true;
            bRet = FmNative.stopScan();
        }
        return bRet;
    }

    /**
     * Check FM is in seek progress or not
     *
     * @return true if in seek progress, otherwise return false.
     */
    public boolean isSeeking() {
        return mIsNativeSeeking;
    }

    /**
     * Set RDS
     *
     * @param on true, enable RDS; false, disable RDS.
     */
    public void setRdsAsync(boolean on) {
        final int bundleSize = 1;
        mFmServiceHandler.removeMessages(FmListener.MSGID_SET_RDS_FINISHED);
        Bundle bundle = new Bundle(bundleSize);
        bundle.putBoolean(OPTION, on);
        Message msg = mFmServiceHandler.obtainMessage(FmListener.MSGID_SET_RDS_FINISHED);
        msg.setData(bundle);
        mFmServiceHandler.sendMessage(msg);
    }

    private int setRds(boolean on) {
        if (mPowerStatus != POWER_UP) {
            return -1;
        }
        int ret = -1;
        if (isRdsSupported()) {
            ret = FmNative.setRds(on);
        }
        return ret;
    }

    /**
     * Get PS information
     *
     * @return PS information
     */
    public String getPs() {
        return mPsString;
    }

    /**
     * Get RT information
     *
     * @return RT information
     */
    public String getRtText() {
        return mRtTextString;
    }

    /**
     * Get AF frequency
     *
     * @return AF frequency
     */
    public void activeAfAsync() {
        mFmServiceHandler.removeMessages(FmListener.MSGID_ACTIVE_AF_FINISHED);
        mFmServiceHandler.sendEmptyMessage(FmListener.MSGID_ACTIVE_AF_FINISHED);
    }

    private int activeAf() {
        if (mPowerStatus != POWER_UP) {
            Log.w(TAG, "activeAf, FM is not powered up");
            return -1;
        }

        int frequency = FmNative.activeAf();
        return frequency;
    }

    /**
     * Mute or unmute FM voice
     *
     * @param mute true for mute, false for unmute
     *
     * @return (true, success; false, failed)
     */
    public void setMuteAsync(boolean mute) {
        mFmServiceHandler.removeMessages(FmListener.MSGID_SET_MUTE_FINISHED);
        final int bundleSize = 1;
        Bundle bundle = new Bundle(bundleSize);
        bundle.putBoolean(OPTION, mute);
        Message msg = mFmServiceHandler.obtainMessage(FmListener.MSGID_SET_MUTE_FINISHED);
        msg.setData(bundle);
        mFmServiceHandler.sendMessage(msg);
    }

    /**
     * Mute or unmute FM voice
     *
     * @param mute true for mute, false for unmute
     *
     * @return (1, success; other, failed)
     */
    public int setMute(boolean mute) {
        if (mPowerStatus != POWER_UP) {
            Log.w(TAG, "setMute, FM is not powered up");
            return -1;
        }
        int iRet = FmNative.setMute(mute);
        mIsMuted = mute;
        return iRet;
    }

    /**
     * Check the latest status is mute or not
     *
     * @return (true, mute; false, unmute)
     */
    public boolean isMuted() {
        return mIsMuted;
    }

    /**
     * Check whether RDS is support in driver
     *
     * @return (true, support; false, not support)
     */
    public boolean isRdsSupported() {
        boolean isRdsSupported = (FmNative.isRdsSupport() == 1);
        return isRdsSupported;
    }

    /**
     * Check whether speaker used or not
     *
     * @return true if use speaker, otherwise return false
     */
    public boolean isSpeakerUsed() {
        return mIsSpeakerUsed;
    }

    /**
     * Initial service and current station
     *
     * @param iCurrentStation current station frequency
     */
    public void initService(int iCurrentStation) {
        mIsServiceInited = true;
        mCurrentStation = iCurrentStation;
    }

    /**
     * Check service is initialed or not
     *
     * @return true if initialed, otherwise return false
     */
    public boolean isServiceInited() {
        return mIsServiceInited;
    }

    /**
     * Get FM service current station frequency
     *
     * @return Current station frequency
     */
    public int getFrequency() {
        return mCurrentStation;
    }

    /**
     * Set FM service station frequency
     *
     * @param station Current station
     */
    public void setFrequency(int station) {
        mCurrentStation = station;
    }

    /**
     * resume FM audio
     */
    private void resumeFmAudio() {
        // If not check mIsAudioFocusHeld && power up, when scan canceled,
        // this will be resume first, then execute power down. it will cause
        // nosise.
        if (mIsAudioFocusHeld && isPlaying()) {
            enableFmAudio(true);
        }
    }

    /**
     * Switch antenna There are two types of antenna(long and short) If long
     * antenna(most is this type), must plug in earphone as antenna to receive
     * FM. If short antenna, means there is a short antenna if phone already,
     * can receive FM without earphone.
     *
     * @param antenna antenna (0, long antenna, 1 short antenna)
     *
     * @return (0, success; 1 failed; 2 not support)
     */
    public void switchAntennaAsync(int antenna) {
        final int bundleSize = 1;
        mFmServiceHandler.removeMessages(FmListener.MSGID_SWITCH_ANTENNA);

        Bundle bundle = new Bundle(bundleSize);
        bundle.putInt(FmListener.SWITCH_ANTENNA_VALUE, antenna);
        Message msg = mFmServiceHandler.obtainMessage(FmListener.MSGID_SWITCH_ANTENNA);
        msg.setData(bundle);
        mFmServiceHandler.sendMessage(msg);
    }

    /**
     * Need native support whether antenna support interface.
     *
     * @param antenna antenna (0, long antenna, 1 short antenna)
     *
     * @return (0, success; 1 failed; 2 not support)
     */
    private int switchAntenna(int antenna) {
        // if fm not powerup, switchAntenna will flag whether has earphone
        int ret = FmNative.switchAntenna(antenna);
        return ret;
    }

    /**
     * Start recording
     */
    public void startRecordingAsync() {
        mFmServiceHandler.removeMessages(FmListener.MSGID_STARTRECORDING_FINISHED);
        mFmServiceHandler.sendEmptyMessage(FmListener.MSGID_STARTRECORDING_FINISHED);
    }

    private void startRecording() {
        sRecordingSdcard = FmUtils.getDefaultStoragePath();
        if (sRecordingSdcard == null || sRecordingSdcard.isEmpty()) {
            Log.d(TAG, "startRecording, may be no sdcard");
            onRecorderError(FmRecorder.ERROR_SDCARD_NOT_PRESENT);
            return;
        }

        if (mFmRecorder == null) {
            mFmRecorder = new FmRecorder(mAudioRecord.getFormat());
            mFmRecorder.registerRecorderStateListener(FmService.this);
        }

        if (isSdcardReady(sRecordingSdcard)) {
            mFmRecorder.startRecording(mContext);
            if (mAudioPatch != null) {
                Log.d(TAG, "Switching to SW rendering on recording start");
                releaseAudioPatch();
                startRender();
            }
        } else {
            onRecorderError(FmRecorder.ERROR_SDCARD_NOT_PRESENT);
        }
    }

    private boolean isSdcardReady(String sdcardPath) {
        if (!mSdcardStateMap.isEmpty()) {
            if (mSdcardStateMap.get(sdcardPath) != null && !mSdcardStateMap.get(sdcardPath)) {
                Log.d(TAG, "isSdcardReady, return false");
                return false;
            }
        }
        return true;
    }

    /**
     * stop recording
     */
    public void stopRecordingAsync() {
        mFmServiceHandler.removeMessages(FmListener.MSGID_STOPRECORDING_FINISHED);
        mFmServiceHandler.sendEmptyMessage(FmListener.MSGID_STOPRECORDING_FINISHED);
    }

    private boolean stopRecording() {
        if (mFmRecorder == null) {
            Log.e(TAG, "stopRecording, called without a valid recorder!!");
            return false;
        }
        synchronized (mStopRecordingLock) {
            mFmRecorder.stopRecording();
        }
        return true;
    }

    /**
     * Save recording file according name or discard recording file if name is
     * null
     *
     * @param newName New recording file name
     */
    public void saveRecordingAsync(String newName) {
        mFmServiceHandler.removeMessages(FmListener.MSGID_SAVERECORDING_FINISHED);
        final int bundleSize = 1;
        Bundle bundle = new Bundle(bundleSize);
        bundle.putString(RECODING_FILE_NAME, newName);
        Message msg = mFmServiceHandler.obtainMessage(FmListener.MSGID_SAVERECORDING_FINISHED);
        msg.setData(bundle);
        mFmServiceHandler.sendMessage(msg);
    }

    private void saveRecording(String newName) {
        if (mFmRecorder != null) {
            if (newName != null) {
                mFmRecorder.saveRecording(FmService.this, newName);
                return;
            }
            mFmRecorder.discardRecording();
        }
    }

    /**
     * Get record time
     *
     * @return Record time
     */
    public long getRecordTime() {
        if (mFmRecorder != null) {
            return mFmRecorder.getRecordTime();
        }
        return 0;
    }


    /**
     * Get record file size
     */
    public long getFileSize() {
        if (mFmRecorder != null) {
            return mFmRecorder.getFileSize();
        }
        return 0;
    }

    /**
     * Set recording mode
     *
     * @param isRecording true, enter recoding mode; false, exit recording mode
     */
    public void setRecordingModeAsync(boolean isRecording) {
        mFmServiceHandler.removeMessages(FmListener.MSGID_RECORD_MODE_CHANED);
        final int bundleSize = 1;
        Bundle bundle = new Bundle(bundleSize);
        bundle.putBoolean(OPTION, isRecording);
        Message msg = mFmServiceHandler.obtainMessage(FmListener.MSGID_RECORD_MODE_CHANED);
        msg.setData(bundle);
        mFmServiceHandler.sendMessage(msg);
    }

    private void setRecordingMode(boolean isRecording) {
        mIsInRecordingMode = isRecording;
        if (mFmRecorder != null) {
            if (!isRecording) {
                if (mFmRecorder.getState() != FmRecorder.STATE_IDLE) {
                    mFmRecorder.stopRecording();
                }
                resumeFmAudio();
                setMute(false);
                return;
            }
            // reset recorder to unused status
            mFmRecorder.resetRecorder();
        }
    }

    /**
     * Get current recording mode
     *
     * @return if in recording mode return true, otherwise return false;
     */
    public boolean getRecordingMode() {
        return mIsInRecordingMode;
    }

    /**
     * Get record state
     *
     * @return record state
     */
    public int getRecorderState() {
        if (null != mFmRecorder) {
            return mFmRecorder.getState();
        }
        return FmRecorder.STATE_IDLE;
    }

    /**
     * Get recording file name
     *
     * @return recording file name
     */
    public String getRecordingName() {
        if (null != mFmRecorder) {
            return mFmRecorder.getRecordFileName();
        }
        return null;
    }

    @Override
    public void onCreate() {
        super.onCreate();
        mContext = getApplicationContext();
        mAudioManager = (AudioManager) getSystemService(Context.AUDIO_SERVICE);
        mActivityManager = (ActivityManager) getSystemService(Context.ACTIVITY_SERVICE);
        PowerManager powerManager = (PowerManager) getSystemService(Context.POWER_SERVICE);
        mWakeLock = powerManager.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, TAG);
        mWakeLock.setReferenceCounted(false);
        sRecordingSdcard = FmUtils.getDefaultStoragePath();

        registerFmBroadcastReceiver();
        registerSdcardReceiver();
        registerAudioPortUpdateListener();

        HandlerThread handlerThread = new HandlerThread("FmRadioServiceThread");
        handlerThread.start();
        mFmServiceHandler = new FmRadioServiceHandler(handlerThread.getLooper());

        openDevice();
        // set speaker to default status, avoid setting->clear data.
        setForceUse(mIsSpeakerUsed);

        setUpMediaSession();

        initAudioRecordSink();
        createRenderThread();
    }

    private void registerAudioPortUpdateListener() {
        if (mAudioPortUpdateListener == null) {
            mAudioPortUpdateListener = new FmOnAudioPortUpdateListener();
            mAudioManager.registerAudioPortUpdateListener(mAudioPortUpdateListener);
        }
    }

    private void unregisterAudioPortUpdateListener() {
        if (mAudioPortUpdateListener != null) {
            mAudioManager.unregisterAudioPortUpdateListener(mAudioPortUpdateListener);
            mAudioPortUpdateListener = null;
        }
    }

    // This function may be called in different threads.
    // Need to add "synchronized" to make sure mAudioRecord and mAudioTrack are the newest.
    // Thread 1: onCreate() or startRender()
    // Thread 2: onAudioPatchListUpdate() or startRender()
    private synchronized void initAudioRecordSink() {
        mAudioRecord = new AudioRecord(MediaRecorder.AudioSource.RADIO_TUNER,
                SAMPLE_RATE, CHANNEL_CONFIG, AUDIO_FORMAT, RECORD_BUF_SIZE);
        mAudioTrack = new AudioTrack(AudioManager.STREAM_MUSIC,
                SAMPLE_RATE, CHANNEL_CONFIG, AUDIO_FORMAT, RECORD_BUF_SIZE, AudioTrack.MODE_STREAM);
    }

    private synchronized int createAudioPatch() {
        Log.d(TAG, "createAudioPatch");
        int status = AudioManager.SUCCESS;
        if (mAudioPatch != null) {
            Log.d(TAG, "createAudioPatch, mAudioPatch is not null, return");
            return status;
        }

        mAudioSource = null;
        mAudioSink = null;
        ArrayList<AudioPort> ports = new ArrayList<AudioPort>();
        mAudioManager.listAudioPorts(ports);
        for (AudioPort port : ports) {
            if (port instanceof AudioDevicePort) {
                int type = ((AudioDevicePort) port).type();
                String name = AudioSystem.getOutputDeviceName(type);
                if (type == AudioSystem.DEVICE_IN_FM_TUNER) {
                    mAudioSource = (AudioDevicePort) port;
                } else if (type == AudioSystem.DEVICE_OUT_WIRED_HEADSET ||
                        type == AudioSystem.DEVICE_OUT_WIRED_HEADPHONE) {
                    mAudioSink = (AudioDevicePort) port;
                }
            }
        }
        if (mAudioSource != null && mAudioSink != null) {
            AudioDevicePortConfig sourceConfig = (AudioDevicePortConfig) mAudioSource
                    .activeConfig();
            AudioDevicePortConfig sinkConfig = (AudioDevicePortConfig) mAudioSink.activeConfig();
            AudioPatch[] audioPatchArray = new AudioPatch[] {null};
            status = mAudioManager.createAudioPatch(audioPatchArray,
                    new AudioPortConfig[] {sourceConfig},
                    new AudioPortConfig[] {sinkConfig});
            mAudioPatch = audioPatchArray[0];
        }
        return status;
    }

    private FmOnAudioPortUpdateListener mAudioPortUpdateListener = null;

    private class FmOnAudioPortUpdateListener implements OnAudioPortUpdateListener {
        /**
         * Callback method called upon audio port list update.
         * @param portList the updated list of audio ports
         */
        @Override
        public void onAudioPortListUpdate(AudioPort[] portList) {
            // Ingore audio port update
        }

        /**
         * Callback method called upon audio patch list update.
         *
         * @param patchList the updated list of audio patches
         */
        @Override
        public void onAudioPatchListUpdate(AudioPatch[] patchList) {
            if (mPowerStatus != POWER_UP) {
                Log.d(TAG, "onAudioPatchListUpdate, not power up");
                return;
            }

            if (!mIsAudioFocusHeld) {
                Log.d(TAG, "onAudioPatchListUpdate no audio focus");
                return;
            }

            if (mAudioPatch != null) {
                ArrayList<AudioPatch> patches = new ArrayList<AudioPatch>();
                mAudioManager.listAudioPatches(patches);
                // When BT or WFD is connected, native will remove the patch (mixer -> device).
                // Need to recreate AudioRecord and AudioTrack for this case.
                if (isPatchMixerToDeviceRemoved(patches)) {
                    Log.d(TAG, "onAudioPatchListUpdate reinit for BT or WFD connected");
                    startRender();
                    return;
                }
                if (isPatchMixerToEarphone(patches)) {
                    stopRender();
                } else {
                    releaseAudioPatch();
                    startRender();
                }
            } else if (mIsRender) {
                ArrayList<AudioPatch> patches = new ArrayList<AudioPatch>();
                mAudioManager.listAudioPatches(patches);
                if (isPatchMixerToEarphone(patches)) {
                    int status;
                    stopAudioTrack();
                    stopRender();
                    status = createAudioPatch();
                    if (status != AudioManager.SUCCESS){
                       Log.d(TAG, "onAudioPatchListUpdate: fallback as createAudioPatch failed");
                       startRender();
                    }
                }
            }
        }

        /**
         * Callback method called when the mediaserver dies
         */
        @Override
        public void onServiceDied() {
            enableFmAudio(false);
        }
    }

    private synchronized void releaseAudioPatch() {
        if (mAudioPatch != null) {
            Log.d(TAG, "releaseAudioPatch");
            mAudioManager.releaseAudioPatch(mAudioPatch);
            mAudioPatch = null;
        }
        mAudioSource = null;
        mAudioSink = null;
    }

    private void registerFmBroadcastReceiver() {
        IntentFilter filter = new IntentFilter();
        filter.addAction(SOUND_POWER_DOWN_MSG);
        filter.addAction(Intent.ACTION_SHUTDOWN);
        filter.addAction(Intent.ACTION_SCREEN_ON);
        filter.addAction(Intent.ACTION_SCREEN_OFF);
        filter.addAction(Intent.ACTION_HEADSET_PLUG);
        mBroadcastReceiver = new FmServiceBroadcastReceiver();
        registerReceiver(mBroadcastReceiver, filter);
    }

    private void unregisterFmBroadcastReceiver() {
        if (null != mBroadcastReceiver) {
            unregisterReceiver(mBroadcastReceiver);
            mBroadcastReceiver = null;
        }
    }

    @Override
    public void onDestroy() {
        mAudioManager.setParameters("AudioFmPreStop=1");
        setMute(true);
        // stop rds first, avoid blocking other native method
        if (isRdsSupported()) {
            stopRdsThread();
        }
        unregisterFmBroadcastReceiver();
        unregisterSdcardListener();
        abandonAudioFocus();
        exitFm();
        if (null != mFmRecorder) {
            mFmRecorder = null;
        }
        removeNotification();
        mSession.setActive(false);
        stopRender();
        exitRenderThread();
        releaseAudioPatch();
        unregisterAudioPortUpdateListener();
        super.onDestroy();
    }

    /**
     * Exit FMRadio application
     */
    private void exitFm() {
        mIsAudioFocusHeld = false;
        // Stop FM recorder if it is working
        if (null != mFmRecorder) {
            synchronized (mStopRecordingLock) {
                int fmState = mFmRecorder.getState();
                if (FmRecorder.STATE_RECORDING == fmState) {
                    mFmRecorder.stopRecording();
                }
            }
        }

        // When exit, we set the audio path back to earphone.
        if (mIsNativeScanning || mIsNativeSeeking) {
            stopScan();
        }

        mFmServiceHandler.removeCallbacksAndMessages(null);
        mFmServiceHandler.removeMessages(FmListener.MSGID_FM_EXIT);
        mFmServiceHandler.sendEmptyMessage(FmListener.MSGID_FM_EXIT);
    }

    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        // Change the notification string.
        if (isPlaying()) {
            showPlayingNotification();
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        int ret = super.onStartCommand(intent, flags, startId);

        if (intent != null) {
            String action = intent.getAction();
            if (FM_SEEK_PREVIOUS.equals(action)) {
                seekStationAsync(FmUtils.computeFrequency(mCurrentStation), false);
            } else if (FM_SEEK_NEXT.equals(action)) {
                seekStationAsync(FmUtils.computeFrequency(mCurrentStation), true);
            } else if (FM_TURN_ON.equals(action)) {
                powerUpAsync(FmUtils.computeFrequency(mCurrentStation));
            } else if (FM_TURN_OFF.equals(action)) {
                powerDownAsync();
            }
        }
        return START_NOT_STICKY;
    }

    public boolean isPlaying() {
        return mPowerStatus == POWER_UP;
    }

    /**
     * Start RDS thread to update RDS information
     */
    private void startRdsThread() {
        mIsRdsThreadExit = false;
        if (null != mRdsThread) {
            return;
        }
        mRdsThread = new Thread() {
            public void run() {
                while (true) {
                    if (mIsRdsThreadExit) {
                        break;
                    }

                    int iRdsEvents = FmNative.readRds();
                    if (iRdsEvents != 0) {
                        Log.d(TAG, "startRdsThread, is rds events: " + iRdsEvents);
                    }

                    if (RDS_EVENT_PROGRAMNAME == (RDS_EVENT_PROGRAMNAME & iRdsEvents)) {
                        byte[] bytePS = FmNative.getPs();
                        if (null != bytePS) {
                            String ps = new String(bytePS).trim();
                            if (!mPsString.equals(ps)) {
                                updatePlayingNotification();
                            }
                            ContentValues values = null;
                            if (FmStation.isStationExist(mContext, mCurrentStation)) {
                                values = new ContentValues(1);
                                values.put(Station.PROGRAM_SERVICE, ps);
                                FmStation.updateStationToDb(mContext, mCurrentStation, values);
                            } else {
                                values = new ContentValues(2);
                                values.put(Station.FREQUENCY, mCurrentStation);
                                values.put(Station.PROGRAM_SERVICE, ps);
                                FmStation.insertStationToDb(mContext, values);
                            }
                            if (isActivityForeground()) {
                                setPs(ps);
                            }
                        }
                    }

                    if (RDS_EVENT_LAST_RADIOTEXT == (RDS_EVENT_LAST_RADIOTEXT & iRdsEvents)) {
                        byte[] byteLRText = FmNative.getLrText();
                        if (null != byteLRText) {
                            String rds = new String(byteLRText).trim();
                            if (!mRtTextString.equals(rds)) {
                                updatePlayingNotification();
                            }
                            if (isActivityForeground()) {
                                setLRText(rds);
                            }
                            ContentValues values = null;
                            if (FmStation.isStationExist(mContext, mCurrentStation)) {
                                values = new ContentValues(1);
                                values.put(Station.RADIO_TEXT, rds);
                                FmStation.updateStationToDb(mContext, mCurrentStation, values);
                            } else {
                                values = new ContentValues(2);
                                values.put(Station.FREQUENCY, mCurrentStation);
                                values.put(Station.RADIO_TEXT, rds);
                                FmStation.insertStationToDb(mContext, values);
                            }
                        }
                    }

                    if (RDS_EVENT_AF == (RDS_EVENT_AF & iRdsEvents)) {
                        /*
                         * add for rds AF
                         */
                        if (mIsScanning || mIsSeeking) {
                            Log.d(TAG, "startRdsThread, seek or scan going, no need to tune here");
                        } else if (mPowerStatus == POWER_DOWN) {
                            Log.d(TAG, "startRdsThread, fm is power down, do nothing.");
                        } else {
                            int iFreq = FmNative.activeAf();
                            if (FmUtils.isValidStation(iFreq)) {
                                // if the new frequency is not equal to current
                                // frequency.
                                if (mCurrentStation != iFreq) {
                                    if (!mIsScanning && !mIsSeeking) {
                                        Log.d(TAG, "startRdsThread, seek or scan not going,"
                                                + "need to tune here");
                                        tuneStationAsync(FmUtils.computeFrequency(iFreq));
                                    }
                                }
                            }
                        }
                    }
                    // Do not handle other events.
                    // Sleep 500ms to reduce inquiry frequency
                    try {
                        final int hundredMillisecond = 500;
                        Thread.sleep(hundredMillisecond);
                    } catch (InterruptedException e) {
                        e.printStackTrace();
                    }
                }
            }
        };
        mRdsThread.start();
    }

    /**
     * Stop RDS thread to stop listen station RDS change
     */
    private void stopRdsThread() {
        if (null != mRdsThread) {
            // Must call closedev after stopRDSThread.
            mIsRdsThreadExit = true;
            mRdsThread = null;
        }
    }

    /**
     * Set PS information
     *
     * @param ps The ps information
     */
    private void setPs(String ps) {
        if (0 != mPsString.compareTo(ps)) {
            mPsString = ps;
            Bundle bundle = new Bundle(3);
            bundle.putInt(FmListener.CALLBACK_FLAG, FmListener.LISTEN_PS_CHANGED);
            bundle.putString(FmListener.KEY_PS_INFO, mPsString);
            notifyActivityStateChanged(bundle);
        } // else New PS is the same as current
    }

    /**
     * Set RT information
     *
     * @param lrtText The RT information
     */
    private void setLRText(String lrtText) {
        if (0 != mRtTextString.compareTo(lrtText)) {
            mRtTextString = lrtText;
            Bundle bundle = new Bundle(3);
            bundle.putInt(FmListener.CALLBACK_FLAG, FmListener.LISTEN_RT_CHANGED);
            bundle.putString(FmListener.KEY_RT_INFO, mRtTextString);
            notifyActivityStateChanged(bundle);
        } // else New RT is the same as current
    }

    /**
     * Open or close FM Radio audio
     *
     * @param enable true, open FM audio; false, close FM audio;
     */
    private void enableFmAudio(boolean enable) {
        if (enable) {
            if ((mPowerStatus != POWER_UP) || !mIsAudioFocusHeld) {
                Log.d(TAG, "enableFmAudio, current not available return.mIsAudioFocusHeld:"
                    + mIsAudioFocusHeld);
                return;
            }

            startAudioTrack();
            startPatchOrRender();
        } else {
            releaseAudioPatch();
            stopRender();
        }
    }

    private void startPatchOrRender() {
        ArrayList<AudioPatch> patches = new ArrayList<AudioPatch>();
        mAudioManager.listAudioPatches(patches);
        if (mAudioPatch == null) {
            if (isPatchMixerToEarphone(patches)) {
                int status;
                stopAudioTrack();
                stopRender();
                status = createAudioPatch();
                if (status != AudioManager.SUCCESS){
                   Log.d(TAG, "startPatchOrRender: fallback as createAudioPatch failed");
                   startRender();
                }
            } else {
                if (!isRendering()) {
                    startRender();
                }
            }
        }
    }

    // Make sure patches count will not be 0
    private boolean isPatchMixerToEarphone(ArrayList<AudioPatch> patches) {
        int deviceCount = 0;
        int deviceEarphoneCount = 0;

        if (getRecorderState() == FmRecorder.STATE_RECORDING) {
            // force software rendering when recording
            return false;
        }

        if (mContext.getResources().getBoolean(R.bool.config_useSoftwareRenderingForAudio)) {
            Log.w(TAG, "FIXME: forcing isPatchMixerToEarphone to return false. "
                    + "Software rendering will be used.");
            return false;
        } else {
            for (AudioPatch patch : patches) {
                AudioPortConfig[] sources = patch.sources();
                AudioPortConfig[] sinks = patch.sinks();
                AudioPortConfig sourceConfig = sources[0];
                AudioPortConfig sinkConfig = sinks[0];
                AudioPort sourcePort = sourceConfig.port();
                AudioPort sinkPort = sinkConfig.port();
                Log.d(TAG, "isPatchMixerToEarphone " + sourcePort + " ====> " + sinkPort);
                if (sourcePort instanceof AudioMixPort && sinkPort instanceof AudioDevicePort) {
                    deviceCount++;
                    int type = ((AudioDevicePort) sinkPort).type();
                    if (type == AudioSystem.DEVICE_OUT_WIRED_HEADSET ||
                            type == AudioSystem.DEVICE_OUT_WIRED_HEADPHONE) {
                        deviceEarphoneCount++;
                    }
                }
            }
            if (deviceEarphoneCount == 1 && deviceCount == deviceEarphoneCount) {
                return true;
            }
        }
        return false;
    }

    // Check whether the patch (mixer -> device) is removed by native.
    // If no patch (mixer -> device), return true.
    private boolean isPatchMixerToDeviceRemoved(ArrayList<AudioPatch> patches) {
        boolean noMixerToDevice = true;
        for (AudioPatch patch : patches) {
            AudioPortConfig[] sources = patch.sources();
            AudioPortConfig[] sinks = patch.sinks();
            AudioPortConfig sourceConfig = sources[0];
            AudioPortConfig sinkConfig = sinks[0];
            AudioPort sourcePort = sourceConfig.port();
            AudioPort sinkPort = sinkConfig.port();

            if (sourcePort instanceof AudioMixPort && sinkPort instanceof AudioDevicePort) {
                noMixerToDevice = false;
                break;
            }
        }
        return noMixerToDevice;
    }

    /**
     * Show notification
     */
    private void showPlayingNotification() {
        if (isActivityForeground() || mIsScanning
                || (getRecorderState() == FmRecorder.STATE_RECORDING)) {
            return;
        }
        synchronized (mNotificationLock) {
            String stationName = "";
            String radioText = "";
            ContentResolver resolver = mContext.getContentResolver();
            Cursor cursor = null;
            try {
                cursor = resolver.query(
                        Station.CONTENT_URI,
                        FmStation.COLUMNS,
                        Station.FREQUENCY + "=?",
                        new String[] { String.valueOf(mCurrentStation) },
                        null);
                if (cursor != null && cursor.moveToFirst()) {
                    // If the station name is not exist, show program service(PS) instead
                    stationName = cursor.getString(cursor.getColumnIndex(Station.STATION_NAME));
                    if (TextUtils.isEmpty(stationName)) {
                        stationName = cursor.getString(cursor.getColumnIndex(Station.PROGRAM_SERVICE));
                    }
                    radioText = cursor.getString(cursor.getColumnIndex(Station.RADIO_TEXT));

                } else {
                    Log.d(TAG, "showPlayingNotification, cursor is null");
                }
            } finally {
                if (cursor != null) {
                    cursor.close();
                }
            }

            Intent aIntent = new Intent(Intent.ACTION_MAIN);
            aIntent.addCategory(Intent.CATEGORY_LAUNCHER);
            aIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            aIntent.setClassName(getPackageName(), mTargetClassName);
            PendingIntent pAIntent = PendingIntent.getActivity(mContext, 0, aIntent, 0);

            if (mNotificationManager == null) {
                mNotificationManager = (NotificationManager)
                    mContext.getSystemService(Context.NOTIFICATION_SERVICE);
            }

            if (mNotificationChannel == null) {
                mNotificationChannel =
                    new NotificationChannel(NOTIFICATION_CHANNEL,
                            mContext.getString(R.string.app_name),
                            NotificationManager.IMPORTANCE_LOW);

                mNotificationManager.createNotificationChannel(mNotificationChannel);
            }

            boolean isPlaying = isPlaying();
            int playButtonResId = isPlaying
                    ? R.drawable.btn_fm_rec_stop_enabled :
                    R.drawable.btn_fm_rec_playback_enabled;
            int playButtonTitleResId = isPlaying
                    ? R.string.accessibility_pause :
                    R.string.accessibility_play;

            long playBackStateActions = PlaybackState.ACTION_PLAY |
                    PlaybackState.ACTION_PLAY_PAUSE |
                    PlaybackState.ACTION_PAUSE |
                    PlaybackState.ACTION_SKIP_TO_NEXT |
                    PlaybackState.ACTION_SKIP_TO_PREVIOUS |
                    PlaybackState.ACTION_STOP;

            mSession.setPlaybackState(new PlaybackState.Builder()
                    .setActions(playBackStateActions)
                    .setState((isPlaying ?
                            PlaybackState.STATE_PLAYING :
                            PlaybackState.STATE_PAUSED), 0, 1.0f).build());

            Notification.Builder notificationBuilder;
            notificationBuilder = new Notification.Builder(mContext, NOTIFICATION_CHANNEL);
            notificationBuilder.setSmallIcon(R.drawable.ic_notification);
            notificationBuilder.setShowWhen(false);
            notificationBuilder.setAutoCancel(true);

            Intent intent = new Intent(FM_SEEK_PREVIOUS);
            intent.setClass(mContext, FmService.class);
            PendingIntent pIntent = PendingIntent.getService(mContext, 0, intent, 0);
            notificationBuilder.addAction(R.drawable.btn_fm_prevstation,
                    getString(R.string.accessibility_prev), pIntent);
            intent = new Intent(isPlaying ? FM_TURN_OFF : FM_TURN_ON);
            intent.setClass(mContext, FmService.class);
            pIntent = PendingIntent.getService(mContext, 0, intent, 0);
            notificationBuilder.addAction(playButtonResId,
                    getString(playButtonTitleResId), pIntent);
            intent = new Intent(FM_SEEK_NEXT);
            intent.setClass(mContext, FmService.class);
            pIntent = PendingIntent.getService(mContext, 0, intent, 0);
            notificationBuilder.addAction(R.drawable.btn_fm_nextstation,
                    getString(R.string.accessibility_next) , pIntent);
            notificationBuilder.setContentIntent(pAIntent);

            final String freq = FmUtils.formatStation(mCurrentStation);
            if (!freq.equals(mCachedArtworkKey)) {
                mCachedArtwork = FmUtils.createNotificationArtwork(mContext, freq);
                mCachedArtworkKey = freq;
            }
            notificationBuilder.setColor(mContext.getResources()
                    .getColor(R.color.notification_icon_bg_color));
            notificationBuilder.setLargeIcon(mCachedArtwork);

            // Show FM Radio if empty
            if (TextUtils.isEmpty(stationName)) {
                stationName = getString(R.string.app_name);
            }

            mSession.setMetadata(new MediaMetadata.Builder()
                    .putString(MediaMetadata.METADATA_KEY_ARTIST, radioText)
                    .putString(MediaMetadata.METADATA_KEY_TITLE, stationName)
                    .build());

            // Apply the media style template
            notificationBuilder.setStyle(
                    new Notification.MediaStyle()
                    .setShowActionsInCompactView(0, 1, 2)
                    .setMediaSession(mSession.getSessionToken()));
            notificationBuilder.setContentTitle(stationName);
            // If radio text is "" or null, we also need to update notification.
            notificationBuilder.setContentText(radioText);
            Log.d(TAG, "showPlayingNotification PS:" + stationName + ", RT:" + radioText);

            Notification n = notificationBuilder.build();
            n.flags &= ~Notification.FLAG_NO_CLEAR;
            startForeground(NOTIFICATION_ID, n);
        }
    }

    private void setUpMediaSession() {
        mSession = new MediaSession(this, TAG);
        mSession.setActive(true);
        AudioAttributes attrs = new AudioAttributes.Builder()
            .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                .build();
        mSession.setPlaybackToLocal(attrs);
        mSession.setCallback(new MediaSession.Callback() {
            @Override
            public void onPause() {
                powerDownAsync();
            }

            @Override
            public void onPlay() {
                powerUpAsync(FmUtils.computeFrequency(mCurrentStation));
            }

            @Override
            public void onSkipToNext() {
                seekStationAsync(FmUtils.computeFrequency(mCurrentStation), true);
            }

            @Override
            public void onSkipToPrevious() {
                seekStationAsync(FmUtils.computeFrequency(mCurrentStation), false);
            }

            @Override
            public void onStop() {
                powerDownAsync();
            }

            @Override
            public boolean onMediaButtonEvent(Intent mediaButtonIntent) {
                if (mediaButtonIntent != null &&
                        Intent.ACTION_MEDIA_BUTTON.equals(mediaButtonIntent.getAction()))
                {
                    KeyEvent ke = mediaButtonIntent.getParcelableExtra(Intent.EXTRA_KEY_EVENT);
                    if (ke != null && ke.getKeyCode() == KeyEvent.KEYCODE_HEADSETHOOK) {
                        if (ke.getAction() == KeyEvent.ACTION_UP) {
                            handleHeadsetHookClick(ke.getEventTime());
                        }
                        return true;
                    }
                }
                return super.onMediaButtonEvent(mediaButtonIntent);
            }
        });
    }

    private void handleHeadsetHookClick(long timestamp) {
        final int bundleSize = 1;
        Bundle bundle = new Bundle(bundleSize);
        bundle.putLong(FmListener.KEY_HEADSET_HOOK_EVENT, timestamp);
        Message msg = mFmServiceHandler.obtainMessage(FmListener.MSGID_HEADSET_HOOK_EVENT);
        msg.setData(bundle);
        mFmServiceHandler.sendMessage(msg);
    }

    /**
     * Show notification
     */
    public void showRecordingNotification(Notification notification) {
        startForeground(NOTIFICATION_ID, notification);
    }

    /**
     * Remove notification
     */
    public void removeNotification() {
        stopForeground(true);
    }

    /**
     * Update notification
     */
    public void updatePlayingNotification() {
        if (isPlaying()) {
            showPlayingNotification();
        }
    }

    /**
     * Register sdcard listener for record
     */
    private void registerSdcardReceiver() {
        if (mSdcardListener == null) {
            mSdcardListener = new SdcardListener();
        }
        IntentFilter filter = new IntentFilter();
        filter.addDataScheme("file");
        filter.addAction(Intent.ACTION_MEDIA_MOUNTED);
        filter.addAction(Intent.ACTION_MEDIA_UNMOUNTED);
        filter.addAction(Intent.ACTION_MEDIA_EJECT);
        registerReceiver(mSdcardListener, filter);
    }

    private void unregisterSdcardListener() {
        if (null != mSdcardListener) {
            unregisterReceiver(mSdcardListener);
        }
    }

    private void updateSdcardStateMap(Intent intent) {
        String action = intent.getAction();
        String sdcardPath = null;
        Uri mountPointUri = intent.getData();
        if (mountPointUri != null) {
            sdcardPath = mountPointUri.getPath();
            if (sdcardPath != null) {
                if (Intent.ACTION_MEDIA_EJECT.equals(action)) {
                    mSdcardStateMap.put(sdcardPath, false);
                } else if (Intent.ACTION_MEDIA_UNMOUNTED.equals(action)) {
                    mSdcardStateMap.put(sdcardPath, false);
                } else if (Intent.ACTION_MEDIA_MOUNTED.equals(action)) {
                    mSdcardStateMap.put(sdcardPath, true);
                }
            }
        }
    }

    /**
     * Notify FM recorder state
     *
     * @param state The current FM recorder state
     */
    @Override
    public void onRecorderStateChanged(int state) {
        mRecordState = state;
        Bundle bundle = new Bundle(2);
        bundle.putInt(FmListener.CALLBACK_FLAG, FmListener.LISTEN_RECORDSTATE_CHANGED);
        bundle.putInt(FmListener.KEY_RECORDING_STATE, state);
        notifyActivityStateChanged(bundle);

        if (state == FmRecorder.STATE_IDLE) { // stopped recording?
            if (isPlaying()) {
                if (mAudioPatch == null) {
                    // maybe switch to patch if possible
                    startPatchOrRender();
                }
            }
        }
    }

    /**
     * Notify FM recorder error message
     *
     * @param error The recorder error type
     */
    @Override
    public void onRecorderError(int error) {
        // if media server die, will not enable FM audio, and convert to
        // ERROR_PLAYER_INATERNAL, call back to activity showing toast.
        mRecorderErrorType = error;

        Bundle bundle = new Bundle(2);
        bundle.putInt(FmListener.CALLBACK_FLAG, FmListener.LISTEN_RECORDERROR);
        bundle.putInt(FmListener.KEY_RECORDING_ERROR_TYPE, mRecorderErrorType);
        notifyActivityStateChanged(bundle);
    }

    /**
     * Check and go next(play or show tips) after recorder file play
     * back finish.
     * Two cases:
     * 1. With headset  -> play FM
     * 2. Without headset -> show plug in earphone tips
     */
    private void checkState() {
        if (isHeadSetIn()) {
            // with headset
            if (isPlaying()) {
                resumeFmAudio();
                setMute(false);
            } else {
                powerUpAsync(FmUtils.computeFrequency(mCurrentStation));
            }
        } else {
            // without headset need show plug in earphone tips
            switchAntennaAsync(mValueHeadSetPlug);
        }
    }

    /**
     * Check the headset is plug in or plug out
     *
     * @return true for plug in; false for plug out
     */
    public boolean isHeadSetIn() {
        return (0 == mValueHeadSetPlug);
    }

    private void focusChanged(int focusState) {
        mIsAudioFocusHeld = false;
        if (mIsNativeScanning || mIsNativeSeeking) {
            // make stop scan from activity call to service.
            // notifyActivityStateChanged(FMRadioListener.LISTEN_SCAN_CANCELED);
            stopScan();
        }

        // using handler thread to update audio focus state
        updateAudioFocusAync(focusState);
    }

    /**
     * Request audio focus
     *
     * @return true, success; false, fail;
     */
    public boolean requestAudioFocus() {
        if (FmUtils.getIsSpeakerModeOnFocusLost(mContext)) {
            setForceUse(true);
            FmUtils.setIsSpeakerModeOnFocusLost(mContext, false);
        }
        if (mIsAudioFocusHeld) {
            return true;
        }

        int audioFocus = mAudioManager.requestAudioFocus(mAudioFocusChangeListener,
                AudioManager.STREAM_MUSIC, AudioManager.AUDIOFOCUS_GAIN);
        mIsAudioFocusHeld = (AudioManager.AUDIOFOCUS_REQUEST_GRANTED == audioFocus);
        return mIsAudioFocusHeld;
    }

    /**
     * Abandon audio focus
     */
    public void abandonAudioFocus() {
        mAudioManager.abandonAudioFocus(mAudioFocusChangeListener);
        mIsAudioFocusHeld = false;
    }

    /**
     * Use to interact with other voice related app
     */
    private final OnAudioFocusChangeListener mAudioFocusChangeListener =
            new OnAudioFocusChangeListener() {
                /**
                 * Handle audio focus change ensure message FIFO
                 *
                 * @param focusChange audio focus change state
                 */
                @Override
                public void onAudioFocusChange(int focusChange) {
                    Log.d(TAG, "onAudioFocusChange " + focusChange);
                    switch (focusChange) {
                        case AudioManager.AUDIOFOCUS_LOSS:
                            synchronized (this) {
                                mAudioManager.setParameters("AudioFmPreStop=1");
                                setMute(true);
                                focusChanged(AudioManager.AUDIOFOCUS_LOSS);
                            }
                            break;

                        case AudioManager.AUDIOFOCUS_LOSS_TRANSIENT:
                            synchronized (this) {
                                mAudioManager.setParameters("AudioFmPreStop=1");
                                setMute(true);
                                focusChanged(AudioManager.AUDIOFOCUS_LOSS_TRANSIENT);
                            }
                            break;

                        case AudioManager.AUDIOFOCUS_GAIN:
                            synchronized (this) {
                                updateAudioFocusAync(AudioManager.AUDIOFOCUS_GAIN);
                            }
                            break;

                        case AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK:
                            synchronized (this) {
                                updateAudioFocusAync(
                                        AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK);
                            }
                            break;

                        default:
                            break;
                    }
                }
            };

    /**
     * Audio focus changed, will send message to handler thread. synchronized to
     * ensure one message can go in this method.
     *
     * @param focusState AudioManager state
     */
    private synchronized void updateAudioFocusAync(int focusState) {
        final int bundleSize = 1;
        Bundle bundle = new Bundle(bundleSize);
        bundle.putInt(FmListener.KEY_AUDIOFOCUS_CHANGED, focusState);
        Message msg = mFmServiceHandler.obtainMessage(FmListener.MSGID_AUDIOFOCUS_CHANGED);
        msg.setData(bundle);
        mFmServiceHandler.sendMessage(msg);
    }

    /**
     * Audio focus changed, update FM focus state.
     *
     * @param focusState AudioManager state
     */
    private void updateAudioFocus(int focusState) {
        switch (focusState) {
            case AudioManager.AUDIOFOCUS_LOSS:
                mPausedByTransientLossOfFocus = false;
                // play back audio will output with music audio
                // May be affect other recorder app, but the flow can not be
                // execute earlier,
                // It should ensure execute after start/stop record.
                if (mFmRecorder != null) {
                    int fmState = mFmRecorder.getState();
                    // only handle recorder state, not handle playback state
                    if (fmState == FmRecorder.STATE_RECORDING) {
                        mFmServiceHandler.removeMessages(
                                FmListener.MSGID_STARTRECORDING_FINISHED);
                        mFmServiceHandler.removeMessages(
                                FmListener.MSGID_STOPRECORDING_FINISHED);
                        stopRecording();
                    }
                }
                handlePowerDown();
                forceToHeadsetMode();
                break;

            case AudioManager.AUDIOFOCUS_LOSS_TRANSIENT:
                if (isPlaying()) {
                    mPausedByTransientLossOfFocus = true;
                }
                // play back audio will output with music audio
                // May be affect other recorder app, but the flow can not be
                // execute earlier,
                // It should ensure execute after start/stop record.
                if (mFmRecorder != null) {
                    int fmState = mFmRecorder.getState();
                    if (fmState == FmRecorder.STATE_RECORDING) {
                        mFmServiceHandler.removeMessages(
                                FmListener.MSGID_STARTRECORDING_FINISHED);
                        mFmServiceHandler.removeMessages(
                                FmListener.MSGID_STOPRECORDING_FINISHED);
                        stopRecording();
                    }
                }
                handlePowerDown();
                forceToHeadsetMode();
                break;

            case AudioManager.AUDIOFOCUS_GAIN:
                if (FmUtils.getIsSpeakerModeOnFocusLost(mContext)) {
                    setForceUse(true);
                    FmUtils.setIsSpeakerModeOnFocusLost(mContext, false);
                }
                if ((mPowerStatus != POWER_UP) && mPausedByTransientLossOfFocus) {
                    final int bundleSize = 1;
                    mFmServiceHandler.removeMessages(FmListener.MSGID_POWERUP_FINISHED);
                    mFmServiceHandler.removeMessages(FmListener.MSGID_POWERDOWN_FINISHED);
                    Bundle bundle = new Bundle(bundleSize);
                    bundle.putFloat(FM_FREQUENCY, FmUtils.computeFrequency(mCurrentStation));
                    handlePowerUp(bundle);
                }
                setMute(false);
                break;

            case AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK:
                setMute(true);
                break;

            default:
                break;
        }
    }

    private void forceToHeadsetMode() {
        if (mIsSpeakerUsed && isHeadSetIn()) {
            AudioSystem.setForceUse(FOR_PROPRIETARY, AudioSystem.FORCE_NONE);
            // save user's option to shared preferences.
            FmUtils.setIsSpeakerModeOnFocusLost(mContext, true);
        }
    }

    /**
     * FM Radio listener record
     */
    private static class Record {
        int mHashCode; // hash code
        FmListener mCallback; // call back
    }

    /**
     * Register FM Radio listener, activity get service state should call this
     * method register FM Radio listener
     *
     * @param callback FM Radio listener
     */
    public void registerFmRadioListener(FmListener callback) {
        synchronized (mRecords) {
            // register callback in AudioProfileService, if the callback is
            // exist, just replace the event.
            Record record = null;
            int hashCode = callback.hashCode();
            final int n = mRecords.size();
            for (int i = 0; i < n; i++) {
                record = mRecords.get(i);
                if (hashCode == record.mHashCode) {
                    return;
                }
            }
            record = new Record();
            record.mHashCode = hashCode;
            record.mCallback = callback;
            mRecords.add(record);
        }
    }

    /**
     * Call back from service to activity
     *
     * @param bundle The message to activity
     */
    private void notifyActivityStateChanged(Bundle bundle) {
        if (!mRecords.isEmpty()) {
            synchronized (mRecords) {
                Iterator<Record> iterator = mRecords.iterator();
                while (iterator.hasNext()) {
                    Record record = (Record) iterator.next();

                    FmListener listener = record.mCallback;

                    if (listener == null) {
                        iterator.remove();
                        return;
                    }

                    listener.onCallBack(bundle);
                }
            }
        }
    }

    /**
     * Call back from service to the current request activity
     * Scan need only notify FmFavoriteActivity if current is FmFavoriteActivity
     *
     * @param bundle The message to activity
     */
    private void notifyCurrentActivityStateChanged(Bundle bundle) {
        if (!mRecords.isEmpty()) {
            Log.d(TAG, "notifyCurrentActivityStateChanged = " + mRecords.size());
            synchronized (mRecords) {
                if (mRecords.size() > 0) {
                    Record record  = mRecords.get(mRecords.size() - 1);
                    FmListener listener = record.mCallback;
                    if (listener == null) {
                        mRecords.remove(record);
                        return;
                    }
                    listener.onCallBack(bundle);
                }
            }
        }
    }

    /**
     * Unregister FM Radio listener
     *
     * @param callback FM Radio listener
     */
    public void unregisterFmRadioListener(FmListener callback) {
        remove(callback.hashCode());
    }

    /**
     * Remove call back according hash code
     *
     * @param hashCode The call back hash code
     */
    private void remove(int hashCode) {
        synchronized (mRecords) {
            Iterator<Record> iterator = mRecords.iterator();
            while (iterator.hasNext()) {
                Record record = (Record) iterator.next();
                if (record.mHashCode == hashCode) {
                    iterator.remove();
                }
            }
        }
    }

    /**
     * Check recording sd card is unmount
     *
     * @param intent The unmount sd card intent
     *
     * @return true or false indicate whether current recording sd card is
     *         unmount or not
     */
    public boolean isRecordingCardUnmount(Intent intent) {
        String unmountSDCard = intent.getData().toString();
        Log.d(TAG, "unmount sd card file path: " + unmountSDCard);
        return unmountSDCard.equalsIgnoreCase("file://" + sRecordingSdcard) ? true : false;
    }

    private int[] updateStations(int[] stations) {
        Log.d(TAG, "updateStations.firstValidstation:" + Arrays.toString(stations));
        int firstValidstation = mCurrentStation;

        int stationNum = 0;
        if (null != stations) {
            int searchedListSize = stations.length;
            if (mIsDistanceExceed) {
                FmStation.cleanSearchedStations(mContext);
                for (int j = 0; j < searchedListSize; j++) {
                    int freqSearched = stations[j];
                    if (FmUtils.isValidStation(freqSearched) &&
                            !FmStation.isFavoriteStation(mContext, freqSearched)) {
                        FmStation.insertStationToDb(mContext, freqSearched, null);
                    }
                }
            } else {
                // get stations from db
                stationNum = updateDBInLocation(stations);
            }
        }

        Log.d(TAG, "updateStations.firstValidstation:" + firstValidstation +
                ",stationNum:" + stationNum);
        return (new int[] {
                firstValidstation, stationNum
        });
    }

    /**
     * update DB, keep favorite and rds which is searched this time,
     * delete rds from db which is not searched this time.
     * @param stations
     * @return number of valid searched stations
     */
    private int updateDBInLocation(int[] stations) {
        int stationNum = 0;
        int searchedListSize = stations.length;
        ArrayList<Integer> stationsInDB = new ArrayList<Integer>();
        Cursor cursor = null;
        try {
            // get non favorite stations
            cursor = mContext.getContentResolver().query(Station.CONTENT_URI,
                    new String[] { FmStation.Station.FREQUENCY },
                    FmStation.Station.IS_FAVORITE + "=0",
                    null, FmStation.Station.FREQUENCY);
            if ((null != cursor) && cursor.moveToFirst()) {

                do {
                    int freqInDB = cursor.getInt(cursor.getColumnIndex(
                            FmStation.Station.FREQUENCY));
                    stationsInDB.add(freqInDB);
                } while (cursor.moveToNext());

            } else {
                Log.d(TAG, "updateDBInLocation, insertSearchedStation cursor is null");
            }
        } finally {
            if (null != cursor) {
                cursor.close();
            }
        }

        int listSizeInDB = stationsInDB.size();
        // delete station if db frequency is not in searched list
        for (int i = 0; i < listSizeInDB; i++) {
            int freqInDB = stationsInDB.get(i);
            for (int j = 0; j < searchedListSize; j++) {
                int freqSearched = stations[j];
                if (freqInDB == freqSearched) {
                    break;
                }
                if (j == (searchedListSize - 1) && freqInDB != freqSearched) {
                    // delete from db
                    FmStation.deleteStationInDb(mContext, freqInDB);
                }
            }
        }

        // add to db if station is not in db
        for (int j = 0; j < searchedListSize; j++) {
            int freqSearched = stations[j];
            if (FmUtils.isValidStation(freqSearched)) {
                stationNum++;
                if (!stationsInDB.contains(freqSearched)
                        && !FmStation.isFavoriteStation(mContext, freqSearched)) {
                    // insert to db
                    FmStation.insertStationToDb(mContext, freqSearched, "");
                }
            }
        }
        return stationNum;
    }

    /**
     * The background handler
     */
    class FmRadioServiceHandler extends Handler {
        private static final int DOUBLE_CLICK_TIMEOUT = 800;
        private int mHeadsetHookClickCounter = 0;

        public FmRadioServiceHandler(Looper looper) {
            super(looper);
        }

        @Override
        public void handleMessage(Message msg) {
            Bundle bundle;
            boolean isPowerup = false;
            boolean isSwitch = true;

            switch (msg.what) {

                // power up
                case FmListener.MSGID_POWERUP_FINISHED:
                    bundle = msg.getData();
                    handlePowerUp(bundle);
                    mIsSpeakerUsed = !isHeadSetIn();
                    break;

                // power down
                case FmListener.MSGID_POWERDOWN_FINISHED:
                    handlePowerDown();
                    break;

                // fm exit
                case FmListener.MSGID_FM_EXIT:
                    if (mIsSpeakerUsed) {
                        setForceUse(false);
                    }
                    powerDown();
                    closeDevice();

                    bundle = new Bundle(1);
                    bundle.putInt(FmListener.CALLBACK_FLAG, FmListener.MSGID_FM_EXIT);
                    notifyActivityStateChanged(bundle);
                    // Finish favorite when exit FM
                    if (sExitListener != null) {
                        sExitListener.onExit();
                    }
                    break;

                // switch antenna
                case FmListener.MSGID_SWITCH_ANTENNA:
                    bundle = msg.getData();
                    int value = bundle.getInt(FmListener.SWITCH_ANTENNA_VALUE);

                    // if ear phone insert, need dismiss plugin earphone
                    // dialog
                    // if earphone plug out and it is not play recorder
                    // state, show plug dialog.
                    if (0 == value) {
                        mIsSpeakerUsed = false;
                        // powerUpAsync(FMRadioUtils.computeFrequency(mCurrentStation));
                        bundle.putInt(FmListener.CALLBACK_FLAG,
                                FmListener.MSGID_SWITCH_ANTENNA);
                        bundle.putBoolean(FmListener.KEY_IS_SWITCH_ANTENNA, true);
                        notifyActivityStateChanged(bundle);
                    } else {
                        mIsSpeakerUsed = true;
                        // ear phone plug out, and recorder state is not
                        // play recorder state,
                        // show dialog.
                        if (mRecordState != FmRecorder.STATE_PLAYBACK) {
                            bundle.putInt(FmListener.CALLBACK_FLAG,
                                    FmListener.MSGID_SWITCH_ANTENNA);
                            bundle.putBoolean(FmListener.KEY_IS_SWITCH_ANTENNA, false);
                            notifyActivityStateChanged(bundle);
                        }
                    }
                    break;

                // tune to station
                case FmListener.MSGID_TUNE_FINISHED:
                    bundle = msg.getData();
                    float tuneStation = bundle.getFloat(FM_FREQUENCY);
                    boolean isTune = tuneStation(tuneStation);
                    // if tune fail, pass current station to update ui
                    if (!isTune) {
                        tuneStation = FmUtils.computeFrequency(mCurrentStation);
                    }
                    bundle = new Bundle(3);
                    bundle.putInt(FmListener.CALLBACK_FLAG,
                            FmListener.MSGID_TUNE_FINISHED);
                    bundle.putBoolean(FmListener.KEY_IS_TUNE, isTune);
                    bundle.putFloat(FmListener.KEY_TUNE_TO_STATION, tuneStation);
                    notifyActivityStateChanged(bundle);
                    break;

                // seek to station
                case FmListener.MSGID_SEEK_FINISHED:
                    bundle = msg.getData();
                    mIsSeeking = true;
                    float seekStation = seekStation(bundle.getFloat(FM_FREQUENCY),
                            bundle.getBoolean(OPTION));
                    boolean isStationTunningSuccessed = false;
                    int station = FmUtils.computeStation(seekStation);
                    if (FmUtils.isValidStation(station)) {
                        isStationTunningSuccessed = tuneStation(seekStation);
                    }
                    // if tune fail, pass current station to update ui
                    if (!isStationTunningSuccessed) {
                        seekStation = FmUtils.computeFrequency(mCurrentStation);
                    }
                    bundle = new Bundle(2);
                    bundle.putInt(FmListener.CALLBACK_FLAG,
                            FmListener.MSGID_TUNE_FINISHED);
                    bundle.putBoolean(FmListener.KEY_IS_TUNE, isStationTunningSuccessed);
                    bundle.putFloat(FmListener.KEY_TUNE_TO_STATION, seekStation);
                    notifyActivityStateChanged(bundle);
                    mIsSeeking = false;
                    break;

                // start scan
                case FmListener.MSGID_SCAN_FINISHED:
                    int[] stations = null;
                    int[] result = null;
                    int scanTuneStation = 0;
                    boolean isScan = true;
                    mIsScanning = true;
                    if (powerUp(FmUtils.DEFAULT_STATION_FLOAT)) {
                        stations = startScan();
                    }

                    // check whether cancel scan
                    if ((null != stations) && stations[0] == -100) {
                        isScan = false;
                        result = new int[] {
                                -1, 0
                        };
                    } else {
                        result = updateStations(stations);
                        scanTuneStation = result[0];
                        tuneStation(FmUtils.computeFrequency(mCurrentStation));
                    }

                    /*
                     * if there is stop command when scan, so it needs to mute
                     * fm avoid fm sound come out.
                     */
                    if (mIsAudioFocusHeld) {
                        setMute(false);
                    }
                    bundle = new Bundle(4);
                    bundle.putInt(FmListener.CALLBACK_FLAG,
                            FmListener.MSGID_SCAN_FINISHED);
                    //bundle.putInt(FmListener.KEY_TUNE_TO_STATION, scanTuneStation);
                    bundle.putInt(FmListener.KEY_STATION_NUM, result[1]);
                    bundle.putBoolean(FmListener.KEY_IS_SCAN, isScan);

                    mIsScanning = false;
                    // Only notify the newest request activity
                    notifyCurrentActivityStateChanged(bundle);
                    break;

                // audio focus changed
                case FmListener.MSGID_AUDIOFOCUS_CHANGED:
                    bundle = msg.getData();
                    int focusState = bundle.getInt(FmListener.KEY_AUDIOFOCUS_CHANGED);
                    updateAudioFocus(focusState);
                    break;

                case FmListener.MSGID_SET_RDS_FINISHED:
                    bundle = msg.getData();
                    setRds(bundle.getBoolean(OPTION));
                    break;

                case FmListener.MSGID_SET_MUTE_FINISHED:
                    bundle = msg.getData();
                    setMute(bundle.getBoolean(OPTION));
                    break;

                case FmListener.MSGID_ACTIVE_AF_FINISHED:
                    activeAf();
                    break;

                case FmListener.MSGID_HEADSET_HOOK_EVENT: {
                    bundle = msg.getData();
                    long eventTime = bundle.getLong(FmListener.KEY_HEADSET_HOOK_EVENT);

                    mHeadsetHookClickCounter = Math.min(mHeadsetHookClickCounter + 1, 3);
                    Log.d(TAG, "Got headset click, count = " + mHeadsetHookClickCounter);
                    removeMessages(FmListener.MSGID_HEADSET_HOOK_MULTI_CLICK_TIMEOUT);

                    if (mHeadsetHookClickCounter == 3) {
                        sendEmptyMessage(FmListener.MSGID_HEADSET_HOOK_MULTI_CLICK_TIMEOUT);
                    } else {
                        sendEmptyMessageAtTime(FmListener.MSGID_HEADSET_HOOK_MULTI_CLICK_TIMEOUT,
                                eventTime + DOUBLE_CLICK_TIMEOUT);
                    }
                    break;
                }

                case FmListener.MSGID_HEADSET_HOOK_MULTI_CLICK_TIMEOUT: {
                    Log.d(TAG, "Handling headset click");
                    switch (mHeadsetHookClickCounter) {
                        case 1:
                            if (isPlaying()) {
                                powerDownAsync();
                            } else {
                                powerUpAsync(FmUtils.computeFrequency(mCurrentStation));
                            }
                            break;
                        case 2:
                            seekStationAsync(FmUtils.computeFrequency(mCurrentStation), true);
                            break;
                        case 3:
                            seekStationAsync(FmUtils.computeFrequency(mCurrentStation), false);
                            break;
                    }
                    mHeadsetHookClickCounter = 0;
                    break;
                }

                /********** recording **********/
                case FmListener.MSGID_STARTRECORDING_FINISHED:
                    startRecording();
                    break;

                case FmListener.MSGID_STOPRECORDING_FINISHED:
                    stopRecording();
                    break;

                case FmListener.MSGID_RECORD_MODE_CHANED:
                    bundle = msg.getData();
                    setRecordingMode(bundle.getBoolean(OPTION));
                    break;

                case FmListener.MSGID_SAVERECORDING_FINISHED:
                    bundle = msg.getData();
                    saveRecording(bundle.getString(RECODING_FILE_NAME));
                    break;

                default:
                    break;
            }
        }

    }

    /**
     * handle power down, execute power down and call back to activity.
     */
    private void handlePowerDown() {
        Bundle bundle;
        boolean isPowerdown = powerDown();
        bundle = new Bundle(1);
        bundle.putInt(FmListener.CALLBACK_FLAG, FmListener.MSGID_POWERDOWN_FINISHED);
        notifyActivityStateChanged(bundle);
    }

    /**
     * handle power up, execute power up and call back to activity.
     *
     * @param bundle power up frequency
     */
    private void handlePowerUp(Bundle bundle) {
        boolean isPowerUp = false;
        boolean isSwitch = true;
        float curFrequency = bundle.getFloat(FM_FREQUENCY);

        if (!isAntennaAvailable()) {
            Log.d(TAG, "handlePowerUp, earphone is not ready");
            bundle = new Bundle(2);
            bundle.putInt(FmListener.CALLBACK_FLAG, FmListener.MSGID_SWITCH_ANTENNA);
            bundle.putBoolean(FmListener.KEY_IS_SWITCH_ANTENNA, false);
            notifyActivityStateChanged(bundle);
            return;
        }
        if (powerUp(curFrequency)) {
            if (FmUtils.isFirstTimePlayFm(mContext)) {
                isPowerUp = firstPlaying(curFrequency);
                FmUtils.setIsFirstTimePlayFm(mContext);
            } else {
                isPowerUp = playFrequency(curFrequency);
            }
            mPausedByTransientLossOfFocus = false;
        }
        bundle = new Bundle(2);
        bundle.putInt(FmListener.CALLBACK_FLAG, FmListener.MSGID_POWERUP_FINISHED);
        bundle.putInt(FmListener.KEY_TUNE_TO_STATION, mCurrentStation);
        notifyActivityStateChanged(bundle);
    }

    /**
     * check FM is foreground or background
     */
    public boolean isActivityForeground() {
        return (mIsFmMainForeground || mIsFmFavoriteForeground || mIsFmRecordForeground);
    }

    /**
     * mark FmMainActivity is foreground or not
     * @param isForeground
     */
    public void setFmMainActivityForeground(boolean isForeground) {
        mIsFmMainForeground = isForeground;
    }

    /**
     * mark FmFavoriteActivity activity is foreground or not
     * @param isForeground
     */
    public void setFmFavoriteForeground(boolean isForeground) {
        mIsFmFavoriteForeground = isForeground;
    }

    /**
     * mark FmRecordActivity activity is foreground or not
     * @param isForeground
     */
    public void setFmRecordActivityForeground(boolean isForeground) {
        mIsFmRecordForeground = isForeground;
    }

    /**
     * Get the recording sdcard path when staring record
     *
     * @return sdcard path like "/storage/sdcard0"
     */
    public static String getRecordingSdcard() {
        return sRecordingSdcard;
    }

    /**
     * The listener interface for exit
     */
    public interface OnExitListener {
        /**
         * When Service finish, should notify FmFavoriteActivity to finish
         */
        void onExit();
    }

    /**
     * Register the listener for exit
     *
     * @param listener The listener want to know the exit event
     */
    public static void registerExitListener(OnExitListener listener) {
        sExitListener = listener;
    }

    /**
     * Unregister the listener for exit
     *
     * @param listener The listener want to know the exit event
     */
    public static void unregisterExitListener(OnExitListener listener) {
        sExitListener = null;
    }

    /**
     * Get the latest recording name the show name in save dialog but saved in
     * service
     *
     * @return The latest recording name or null for not modified
     */
    public String getModifiedRecordingName() {
        return mModifiedRecordingName;
    }

    /**
     * Set the latest recording name if modify the default name
     *
     * @param name The latest recording name or null for not modified
     */
    public void setModifiedRecordingName(String name) {
        mModifiedRecordingName = name;
    }

    @Override
    public void onTaskRemoved(Intent rootIntent) {
        exitFm();
        stopSelf();
        super.onTaskRemoved(rootIntent);
    }

    private boolean firstPlaying(float frequency) {
        if (mPowerStatus != POWER_UP) {
            Log.w(TAG, "firstPlaying, FM is not powered up");
            return false;
        }
        boolean isSeekTune = false;
        float seekStation = FmNative.seek(frequency, false);
        int station = FmUtils.computeStation(seekStation);
        if (FmUtils.isValidStation(station)) {
            isSeekTune = FmNative.tune(seekStation);
            if (isSeekTune) {
                playFrequency(seekStation);
            }
        }
        // if tune fail, pass current station to update ui
        if (!isSeekTune) {
            seekStation = FmUtils.computeFrequency(mCurrentStation);
        }
        return isSeekTune;
    }

    /**
     * Set the mIsDistanceExceed
     * @param exceed true is exceed, false is not exceed
     */
    public void setDistanceExceed(boolean exceed) {
        mIsDistanceExceed = exceed;
    }

    /**
     * Set notification class name
     * @param clsName The target class name of activity
     */
    public void setNotificationClsName(String clsName) {
        mTargetClassName = clsName;
    }
}
