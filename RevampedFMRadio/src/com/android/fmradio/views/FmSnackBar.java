/*
 * Copyright (C) 2014 The Android Open Source Project
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

package com.android.fmradio.views;

import android.content.Context;
import android.graphics.PixelFormat;
import android.os.Handler;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.RelativeLayout;
import android.widget.TextView;

import com.android.fmradio.R;

/**
 * The view used to display the customized toast
 *
 * Usage:
 *
 * FmSnackBar snackBar = FmSnackBar.make(context, title, action,
 * listener,FmSnackBar.DEFAULT_DURATION);
 * snackBar.show();
 * snackBar.dismiss();
 */
public final class FmSnackBar extends View {

    private static final String TAG = "FmSnackBar";
    private static final Object LOCK = new Object();
    public static final int DEFAULT_DURATION = 3000;
    public static final int MIN_DURATION = 1000;
    private Context mContext = null;
    private WindowManager.LayoutParams mWindowParams = null;
    private RelativeLayout mLayout = null;
    private boolean mIsDisplayed = false;
    private Button mButton = null;
    private TextView mTextView = null;
    private OnActionTriggerListener mActionListener = null;
    private Handler mHandler = null;
    private int mDuration = DEFAULT_DURATION;

    private final Runnable mDismissionRunnable = new Runnable() {
        @Override
        public void run() {
            FmSnackBar.this.dismiss();
        }
    };

    /**
     * The callback listener, it will called while the action button
     * was set and the action button was clicked
     */
    public interface OnActionTriggerListener {
        /**
         * Action button callback
         */
        void onActionTriggered();
    }

    /**
     * To make a FmSnackBar instance
     *
     * @param context The context instance
     * @param title The notification text
     * @param actionName The action name displayed to end user
     * @param listener The callback listener
     * @param duration The displaying duration
     * @return The FmSnackBar instance
     */
    public static synchronized FmSnackBar make(Context context, String title, String actionName,
            OnActionTriggerListener listener, int duration) {
        FmSnackBar instance = new FmSnackBar(context);
        if (title == null) {
            instance.mTextView.setText("");
        } else {
            instance.mTextView.setText(title);
        }
        if (actionName != null & listener != null) {
            instance.mButton.setText(actionName);
            instance.mActionListener = listener;
            instance.mButton.setVisibility(View.VISIBLE);
        } else {
            instance.mButton.setVisibility(View.GONE);
        }
        if (duration < MIN_DURATION) {
            instance.mDuration = MIN_DURATION;
        } else {
            instance.mDuration = duration;
        }
        return instance;
    }

    private FmSnackBar(Context context) {
        super(context);
        init(context);
    }

    private void init(Context context) {
        mContext = context;
        mHandler = new Handler();
        mLayout = (RelativeLayout) RelativeLayout.inflate(context, R.layout.snackbar, null);
        mWindowParams = new WindowManager.LayoutParams();
        mWindowParams.type = WindowManager.LayoutParams.TYPE_APPLICATION;
        mWindowParams.format = PixelFormat.RGBA_8888;
        mWindowParams.flags = WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE;
        mWindowParams.gravity = Gravity.LEFT | Gravity.BOTTOM;
        mWindowParams.x = 0;
        mWindowParams.y = 0;
        mWindowParams.width = WindowManager.LayoutParams.MATCH_PARENT;
        mWindowParams.height = WindowManager.LayoutParams.WRAP_CONTENT;

        mButton = (Button) mLayout.findViewById(R.id.snackbar_action);
        mButton.setOnClickListener(new OnClickListener() {
            @Override
            public void onClick(View arg0) {
                if (mActionListener != null) {
                    mActionListener.onActionTriggered();
                }
            }
        });
        mButton.setVisibility(View.GONE);

        mTextView = (TextView) mLayout.findViewById(R.id.snackbar_text);
    }

    /**
     * To display the view of FmSnackBar
     */
    public void show() {
        synchronized (LOCK) {
            WindowManager manager = (WindowManager) mContext
                    .getSystemService(Context.WINDOW_SERVICE);
            if (mIsDisplayed) {
                manager.removeViewImmediate(mLayout);
            }
            manager.addView(mLayout, mWindowParams);
            mIsDisplayed = true;
            mHandler.postDelayed(mDismissionRunnable, mDuration);
        }
    }

    /**
     * To dismiss the view of Snackbar
     */
    public void dismiss() {
        synchronized (LOCK) {
            WindowManager manager = (WindowManager) mContext
                    .getSystemService(Context.WINDOW_SERVICE);
            if (mIsDisplayed) {
                try {
                    manager.removeViewImmediate(mLayout);
                } catch (IllegalArgumentException e) {
                    Log.d(TAG, "dismiss, " + e.toString());
                }
            }
            mIsDisplayed = false;
        }
    }
}
