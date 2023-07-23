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

import android.app.Activity;
import android.app.FragmentManager;
import android.app.Notification.Builder;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.ComponentName;
import android.content.ContentResolver;
import android.content.ContentUris;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.database.ContentObserver;
import android.database.Cursor;
import android.graphics.Bitmap;
import android.net.Uri;
import android.os.Environment;
import android.os.Bundle;
import android.os.Handler;
import android.os.Message;
import android.text.TextUtils;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;
import android.widget.Toast;

import com.android.fmradio.FmStation.Station;
import com.android.fmradio.dialogs.FmSaveDialog;
import com.android.fmradio.views.FmVisualizerView;

import java.io.File;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/**
 * This class interact with user, FM recording function.
 */
public class FmRecordActivity extends Activity implements
        FmSaveDialog.OnRecordingDialogClickListener {
    private static final String TAG = "FmRecordActivity";

    private static final String FM_STOP_RECORDING = "fmradio.stop.recording";
    private static final String FM_ENTER_RECORD_SCREEN = "fmradio.enter.record.screen";
    private static final String TAG_SAVE_RECORDINGD = "SaveRecording";
    private static final int MSG_UPDATE_NOTIFICATION = 1000;
    private static final int TIME_BASE = 60;
    private Context mContext;
    private TextView mMintues;
    private TextView mSeconds;
    private TextView mFsize;
    private TextView mFrequency;
    private View mStationInfoLayout;
    private TextView mStationName;
    private TextView mRadioText;
    private Button mStopRecordButton;
    private FmService mService = null;
    private FragmentManager mFragmentManager;
    private boolean mIsInBackground = false;
    private int mRecordState = FmRecorder.STATE_INVALID;
    private boolean mRecordingStarted = false;
    private int mCurrentStation = FmUtils.DEFAULT_STATION;

    // Notification manager
    private static Object mNotificationLock = new Object();
    private NotificationManager mNotificationManager = null;
    private NotificationChannel mNotificationChannel = null;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Log.d(TAG, "onCreate");
        mContext = getApplicationContext();
        mFragmentManager = getFragmentManager();
        setContentView(R.layout.fm_record_activity);

        mMintues = (TextView) findViewById(R.id.minutes);
        mSeconds = (TextView) findViewById(R.id.seconds);

        mFsize = (TextView) findViewById(R.id.file_size);

        mFrequency = (TextView) findViewById(R.id.frequency);
        mStationInfoLayout = findViewById(R.id.station_name_rt);
        mStationName = (TextView) findViewById(R.id.station_name);
        mRadioText = (TextView) findViewById(R.id.radio_text);
        mRadioText.setTextIsSelectable(true);
        mRadioText.setSelected(true);

        mStopRecordButton = (Button) findViewById(R.id.btn_stop_record);
        mStopRecordButton.setEnabled(false);
        mStopRecordButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                // Stop recording and wait service notify stop record state to show dialog
                mService.stopRecordingAsync();
            }
        });

        if (savedInstanceState != null) {
            mCurrentStation = savedInstanceState.getInt(FmStation.CURRENT_STATION);
            mRecordState = savedInstanceState.getInt("last_record_state");
            mRecordingStarted = savedInstanceState.getBoolean("recording_started", false);
        } else {
            Intent intent = getIntent();
            mCurrentStation = intent.getIntExtra(FmStation.CURRENT_STATION,
                    FmUtils.DEFAULT_STATION);
            mRecordState = intent.getIntExtra("last_record_state", FmRecorder.STATE_INVALID);
            mRecordingStarted = intent.getBooleanExtra("recording_started", false);
        }
        bindService(new Intent(this, FmService.class), mServiceConnection,
                Context.BIND_AUTO_CREATE);
        updateUi();
    }

    private void updateUi() {
        // TODO it's on UI thread, change to sub thread
        ContentResolver resolver = mContext.getContentResolver();
        mFrequency.setText(FmUtils.formatStation(mCurrentStation));
        Cursor cursor = null;
        try {
            cursor = resolver.query(
                    Station.CONTENT_URI,
                    FmStation.COLUMNS,
                    Station.FREQUENCY + "=?",
                    new String[] { String.valueOf(mCurrentStation) },
                    null);
            if (cursor != null && cursor.moveToFirst()) {
                // If the station name does not exist, show program service(PS) instead
                String stationName = cursor.getString(cursor.getColumnIndex(Station.STATION_NAME));
                if (TextUtils.isEmpty(stationName)) {
                    stationName = cursor.getString(cursor.getColumnIndex(Station.PROGRAM_SERVICE));
                }
                String radioText = cursor.getString(cursor.getColumnIndex(Station.RADIO_TEXT));
                mStationName.setText(stationName);
                mRadioText.setText(radioText);
                int id = cursor.getInt(cursor.getColumnIndex(Station._ID));

                if (mWatchedId != id) {
                    if (mWatchedId != -1) {
                        resolver.unregisterContentObserver(mContentObserver);
                    }
                    resolver.registerContentObserver(
                            ContentUris.withAppendedId(Station.CONTENT_URI, id),
                            false, mContentObserver);
                    mWatchedId = id;
                }
                // If no station name and no radio text, hide the view
                mStationName.setVisibility(TextUtils.isEmpty(stationName) &&
                        TextUtils.isEmpty(radioText) ? View.GONE : View.VISIBLE);
                mRadioText.setVisibility(TextUtils.isEmpty(radioText) ?
                        View.GONE : View.VISIBLE);

                Log.d(TAG, "updateUi, frequency = " + mCurrentStation + ", stationName = "
                        + stationName + ", radioText = " + radioText);
            }
        } finally {
            if (cursor != null) {
                cursor.close();
            }
        }
    }

    private void updateRecordingNotification(long recordTime) {
        synchronized (mNotificationLock) {
            if (mNotificationManager == null) {
                mNotificationManager = (NotificationManager)
                    mContext.getSystemService(Context.NOTIFICATION_SERVICE);
            }

            if (mNotificationChannel == null) {
                mNotificationChannel =
                    new NotificationChannel(mService.NOTIFICATION_CHANNEL,
                            mContext.getString(R.string.app_name),
                            NotificationManager.IMPORTANCE_LOW);

                mNotificationManager.createNotificationChannel(mNotificationChannel);
            }

            Notification.Builder notificationBuilder;
            Intent intent = new Intent(FM_STOP_RECORDING);
            intent.setClass(mContext, FmRecordActivity.class);
            intent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            PendingIntent pendingIntent = PendingIntent.getActivity(mContext, 0, intent,
                    PendingIntent.FLAG_UPDATE_CURRENT);

            Bitmap largeIcon = FmUtils.createNotificationLargeIcon(mContext,
                    FmUtils.formatStation(mCurrentStation));
            notificationBuilder = new Builder(this, mService.NOTIFICATION_CHANNEL)
                    .setContentText(getText(R.string.record_notification_message))
                    .setShowWhen(false)
                    .setAutoCancel(true)
                    .setSmallIcon(R.drawable.ic_notification)
                    .setLargeIcon(largeIcon)
                    .addAction(R.drawable.btn_fm_rec_stop_enabled, getText(R.string.stop_record),
                            pendingIntent);

            Intent cIntent = new Intent(FM_ENTER_RECORD_SCREEN);
            cIntent.setClass(mContext, FmRecordActivity.class);
            cIntent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            PendingIntent contentPendingIntent = PendingIntent.getActivity(mContext, 0, cIntent,
                    PendingIntent.FLAG_UPDATE_CURRENT);
            notificationBuilder.setContentIntent(contentPendingIntent);
            // Format record time to show on title
            Date date = new Date(recordTime);
            SimpleDateFormat simpleDateFormat = new SimpleDateFormat("mm:ss", Locale.ENGLISH);
            String time = simpleDateFormat.format(date);

            notificationBuilder.setContentTitle(time);
            if (mService != null) {
                mService.showRecordingNotification(notificationBuilder.build());
            }
        }
    }

    @Override
    public void onNewIntent(Intent intent) {
        if (intent != null && intent.getAction() != null) {
            String action = intent.getAction();
            if (FM_STOP_RECORDING.equals(action)) {
                // If click stop button in notification, need to stop recording
                if (mService != null && !isStopRecording()) {
                    mService.stopRecordingAsync();
                }
            } else if (FM_ENTER_RECORD_SCREEN.equals(action)) {
                // Just enter record screen, do nothing
            }
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        mIsInBackground = false;

        onResumeWithService();
    }

    private void onResumeWithService() {

        if (null == mService) {
            // service not yet connected
            return;
        }

        if (mIsInBackground) {
            // not resumed yet
            return;
        }

        mCurrentStation = mService.getFrequency();
        mRecordState = mService.getRecorderState();

        mService.setFmRecordActivityForeground(true);
        removeNotification();
        switch (mRecordState) {
            case FmRecorder.STATE_IDLE:
                if (!mRecordingStarted) {
                    // start the new recording
                    mRecordingStarted = true;
                    mService.startRecordingAsync();
                    break;
                }

                // recording was stopped while we were away
                if (!isSaveDialogShown()) {
                    showSaveDialog();
                    break;
                }

                Log.wtf(TAG, "returned to FmRecordActivity after save dialog");
                finish();
                return;

            case FmRecorder.STATE_RECORDING:
                break;

            case FmRecorder.STATE_INVALID:
            default:
                Log.wtf(TAG, "Unexpected state: " + mRecordState);
                finish();
                return;
        }

        mStopRecordButton.setEnabled(true);
        mHandler.removeMessages(FmListener.MSGID_REFRESH);
        mHandler.sendEmptyMessage(FmListener.MSGID_REFRESH);
    }

    @Override
    protected void onPause() {
        super.onPause();
        mIsInBackground = true;
        if (null != mService) {
            mService.setFmRecordActivityForeground(false);
        }
        // Stop refreshing timer text
        mHandler.removeMessages(FmListener.MSGID_REFRESH);
        // Show notification when switch to background
        showNotification();
    }

    private void showNotification() {
        // If have stopped recording, need not show notification
        if (!isStopRecording()) {
            mHandler.sendEmptyMessage(MSG_UPDATE_NOTIFICATION);
        } else if (isSaveDialogShown()) {
            // Only when save dialog is shown and FM radio is back to background,
            // it is necessary to update playing notification.
            // Otherwise, FmMainActivity will update playing notification.
            mService.updatePlayingNotification();
        }
    }

    private void removeNotification() {
        mHandler.removeMessages(MSG_UPDATE_NOTIFICATION);
        if (mService != null) {
            mService.removeNotification();
            mService.updatePlayingNotification();
        }
    }

    @Override
    protected void onSaveInstanceState(Bundle outState) {
        outState.putInt(FmStation.CURRENT_STATION, mCurrentStation);
        outState.putInt("last_record_state", mRecordState);
        outState.putBoolean("recording_started", mRecordingStarted);
        super.onSaveInstanceState(outState);
    }

    @Override
    protected void onDestroy() {
        removeNotification();
        mHandler.removeCallbacksAndMessages(null);
        if (mService != null) {
            mService.unregisterFmRadioListener(mFmListener);
        }
        unbindService(mServiceConnection);
        if (mWatchedId != -1) {
            mContext.getContentResolver().unregisterContentObserver(mContentObserver);
        }
        super.onDestroy();
    }

    /**
     * Recording dialog click
     *
     * @param recordingName The new recording name
     */
    @Override
    public void onRecordingDialogClick(
            String recordingName) {
        // Happen when activity recreate, such as switch language
        if (mIsInBackground) {
            return;
        }

        if (recordingName != null && mService != null) {
            mService.saveRecordingAsync(recordingName);
            returnResult(recordingName, getString(R.string.toast_record_saved));
        } else {
            returnResult(null, getString(R.string.toast_record_not_saved));
        }
        finish();
    }

    @Override
    public void onBackPressed() {
        if (mService != null & !isStopRecording()) {
            // Stop recording and wait service notify stop record state to show dialog
            mService.stopRecordingAsync();
            return;
        }
        super.onBackPressed();
    }

    private final ServiceConnection mServiceConnection = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName name, android.os.IBinder service) {
            mService = ((FmService.ServiceBinder) service).getService();
            mService.registerFmRadioListener(mFmListener);

            onResumeWithService();
        };

        @Override
        public void onServiceDisconnected(android.content.ComponentName name) {
            mService = null;
        };
    };

    private String addPaddingForString(long time) {
        StringBuilder builder = new StringBuilder();
        if (time >= 0 && time < 10) {
            builder.append("0");
        }
        return builder.append(time).toString();
    }

    private final Handler mHandler = new Handler() {
        @Override
        public void handleMessage(Message msg) {
            switch (msg.what) {
                case FmListener.MSGID_REFRESH:
                    if (mService != null) {
                        long recordTimeInMillis = mService.getRecordTime();
                        long recordTimeInSec = recordTimeInMillis / 1000L;
                        mMintues.setText(addPaddingForString(recordTimeInSec / TIME_BASE));
                        mSeconds.setText(addPaddingForString(recordTimeInSec % TIME_BASE));
                        mFsize.setText(Utils.getHumanReadableSize(mService.getFileSize()));
                        checkStorageSpaceAndStop();
                    }
                    mHandler.sendEmptyMessageDelayed(FmListener.MSGID_REFRESH, 1000);
                    break;

                case MSG_UPDATE_NOTIFICATION:
                    if (mService != null) {
                        updateRecordingNotification(mService.getRecordTime());
                        checkStorageSpaceAndStop();
                    }
                    mHandler.sendEmptyMessageDelayed(MSG_UPDATE_NOTIFICATION, 1000);
                    break;

                case FmListener.LISTEN_RECORDSTATE_CHANGED:
                    // State change from STATE_IDLE to STATE_RECORDING mean begin recording
                    // State change from STATE_RECORDING to STATE_IDLE mean stop recording
                    int newState = mService.getRecorderState();
                    Log.d(TAG, "handleMessage, record state changed: newState = " + newState
                            + ", mRecordState = " + mRecordState);
                    if (mRecordState == FmRecorder.STATE_IDLE
                            && newState == FmRecorder.STATE_RECORDING) {
                        Log.d(TAG, "Recording started");
                    } else if (mRecordState == FmRecorder.STATE_RECORDING
                            && newState == FmRecorder.STATE_IDLE) {
                        Log.d(TAG, "Recording stopped");
                        showSaveDialog();
                    } else {
                        Log.e(TAG, "Unexpected recording state: " + newState);
                    }
                    mRecordState = newState;
                    break;

                case FmListener.LISTEN_RECORDERROR:
                    Bundle bundle = msg.getData();
                    int errorType = bundle.getInt(FmListener.KEY_RECORDING_ERROR_TYPE);
                    handleRecordError(errorType);
                    break;

                default:
                    break;
            }
        };
    };

    private void checkStorageSpaceAndStop() {
        long recordTimeInMillis = mService.getRecordTime();
        long recordTimeInSec = recordTimeInMillis / 1000L;
        // Check storage free space
        String recordingSdcard = FmUtils.getDefaultStoragePath();
        if (!FmUtils.hasEnoughSpace(recordingSdcard)) {
            // Need to record more than 1s.
            // Avoid calling MediaRecorder.stop() before native record starts.
            if (recordTimeInSec >= 1) {
                // Insufficient storage
                mService.stopRecordingAsync();
                Toast.makeText(FmRecordActivity.this,
                        R.string.toast_sdcard_insufficient_space,
                        Toast.LENGTH_SHORT).show();
            }
        }
    }

    private void handleRecordError(int errorType) {
        Log.d(TAG, "handleRecordError, errorType = " + errorType);
        String showString = null;
        switch (errorType) {
            case FmRecorder.ERROR_SDCARD_NOT_PRESENT:
                showString = getString(R.string.toast_sdcard_missing);
                returnResult(null, showString);
                finish();
                break;

            case FmRecorder.ERROR_SDCARD_INSUFFICIENT_SPACE:
                showString = getString(R.string.toast_sdcard_insufficient_space);
                returnResult(null, showString);
                finish();
                break;

            case FmRecorder.ERROR_RECORDER_INTERNAL:
                showString = getString(R.string.toast_recorder_internal_error);
                Toast.makeText(mContext, showString, Toast.LENGTH_SHORT).show();
                break;

            case FmRecorder.ERROR_SDCARD_WRITE_FAILED:
                showString = getString(R.string.toast_recorder_internal_error);
                returnResult(null, showString);
                finish();
                break;

            default:
                Log.w(TAG, "handleRecordError, invalid record error");
                break;
        }
    }

    private void returnResult(String recordName, String resultString) {
        Intent intent = new Intent();
        intent.putExtra(FmMainActivity.EXTRA_RESULT_STRING, resultString);
        if (recordName != null) {
            intent.setData(Uri.parse("file://" + FmService.getRecordingSdcard()
                    + File.separator + Environment.DIRECTORY_RECORDINGS
                    + File.separator + FmRecorder.getFmRecordFolder(mContext) + File.separator
                    + Uri.encode(recordName) + FmRecorder.RECORDING_FILE_EXTENSION));
        }
        setResult(RESULT_OK, intent);
    }

    private int mWatchedId = -1;
    private final ContentObserver mContentObserver = new ContentObserver(new Handler()) {
        public void onChange(boolean selfChange) {
            updateUi();
        };
    };

    // Service listener
    private final FmListener mFmListener = new FmListener() {
        @Override
        public void onCallBack(Bundle bundle) {
            int flag = bundle.getInt(FmListener.CALLBACK_FLAG);
            if (flag == FmListener.MSGID_FM_EXIT) {
                mHandler.removeCallbacksAndMessages(null);
                mRecordState = FmRecorder.STATE_IDLE;
            }

            // remove tag message first, avoid too many same messages in queue.
            Message msg = mHandler.obtainMessage(flag);
            msg.setData(bundle);
            mHandler.removeMessages(flag);
            mHandler.sendMessage(msg);
        }
    };

    /**
     * Show save record dialog
     */
    public void showSaveDialog() {
        removeNotification();
        if (mIsInBackground) {
            Log.d(TAG, "showSaveDialog, activity is in background, show it later");
            return;
        }
        String sdcard = FmService.getRecordingSdcard();
        String recordingName = mService.getRecordingName();
        String saveName = null;
        if (TextUtils.isEmpty(mStationName.getText())) {
            saveName = FmRecorder.RECORDING_FILE_PREFIX +  "_" + recordingName;
        } else {
            saveName = FmRecorder.RECORDING_FILE_PREFIX + "_" + mStationName.getText() + "_"
                    + recordingName;
        }
        FmSaveDialog newFragment = new FmSaveDialog(sdcard, recordingName, saveName);
        newFragment.show(mFragmentManager, TAG_SAVE_RECORDINGD);
        mFragmentManager.executePendingTransactions();
        mHandler.removeMessages(FmListener.MSGID_REFRESH);
    }

    private boolean isStartRecording() {
        return mRecordState == FmRecorder.STATE_RECORDING;
    }

    private boolean isStopRecording() {
        return mRecordState == FmRecorder.STATE_IDLE;
    }

    private boolean isSaveDialogShown() {
        FmSaveDialog saveDialog = (FmSaveDialog)
                mFragmentManager.findFragmentByTag(TAG_SAVE_RECORDINGD);
        return saveDialog != null;
    }
}
