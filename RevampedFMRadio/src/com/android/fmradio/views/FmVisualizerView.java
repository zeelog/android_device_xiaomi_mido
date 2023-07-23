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
import android.content.res.Resources;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.Handler;
import android.util.AttributeSet;
import android.view.View;

import com.android.fmradio.R;
import com.android.fmradio.Utils;

/**
 * The view used to display the visualizer
 */
public final class FmVisualizerView extends View {

    private final Handler mHandler = new Handler();

    private Paint mPaint = new Paint();

    private float mColumnPadding = 3f;

    private boolean mAnimate = false;

    private int mFrequency = 50;

    private static final int COLUME_PADDING_COUNTS = 2;

    private static final int COLUME_COUNTS = 6;

    private static final float[] DEFALT_VISUALIZER_LEVEL = new float[] {
            +0.2f, +0.4f, -0.3f, 1f, +0.7f, -0.2f
    };

    private float[] mPrevLevels = DEFALT_VISUALIZER_LEVEL;

    /**
     * Constructor method
     *
     * @param context The context instance
     * @param attrs The attribute set for this view
     * @param defStyleAttr The default style for this view
     */
    public FmVisualizerView(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        init(context);
    }

    /**
     * Constructor method
     *
     * @param context The context instance
     * @param attrs The attribute set for this view
     */
    public FmVisualizerView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init(context);
    }

    /**
     * Constructor method
     *
     * @param context The context instance
     */
    public FmVisualizerView(Context context) {
        super(context);
        init(context);
    }

    private void init(Context context) {
        final Resources r = context.getResources();
        int bgColor = Utils
            .setColorAlphaComponent(
                    r.getColor(
                        R.color.favorite_station_more_accent_playing_color), 55);
        mPaint.setColor(bgColor);
        mPaint.setAntiAlias(true);
        mPaint.setStrokeWidth(0.3f);
        mPaint.setStrokeCap(Paint.Cap.ROUND);
        mPaint.setStyle(Paint.Style.FILL_AND_STROKE);
        mAnimate = false;
    }

    /**
     * Set the padding between visualizer columns
     *
     * @param padding The padding between visualizer columns
     */
    public void setColumnPadding(int padding) {
        mColumnPadding = padding;
    }

    /**
     * Start the animation
     */
    public void startAnimation() {
        mAnimate = true;
    }

    /**
     * Start the animation
     */
    public void stopAnimation() {
        mAnimate = false;
    }

    /**
     * Whether currently is under animation
     *
     * @return The animation state
     */
    public boolean isAnimated() {
        return mAnimate;
    }

    /**
     * Set the animation frequency
     *
     * @param freguency The specify animation frequency to set
     */
    public void setAnimateFrequency(int freguency) {
        mFrequency = freguency;
    }

    /**
     * Defined to re-freash the view
     */
    private final Runnable mRefreashRunnable = new Runnable() {
        public void run() {
            FmVisualizerView.this.invalidate();
        }
    };

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        canvas.save();
        canvas.drawColor(Color.TRANSPARENT);
        int viewHeight = getHeight();
        int viewWidth = getWidth();
        int paddingLeft = getPaddingLeft();
        int paddingRight = getPaddingRight();
        int paddingTop = getPaddingTop();
        int paddingBottom = getPaddingBottom();
        float colWidth = ((float) (viewWidth - paddingLeft - paddingRight - COLUME_PADDING_COUNTS
                * mColumnPadding))
                / COLUME_COUNTS;
        float colHeight = (float) (viewHeight - paddingBottom - paddingTop);

        float levels[] = new float[COLUME_COUNTS];
        if (!mAnimate) {
            levels = mPrevLevels;
        } else {
            levels = generate(COLUME_COUNTS);
        }
        for (int i = 0; i < COLUME_COUNTS; i++) {
            float left = paddingLeft + i * (mColumnPadding + colWidth);
            float right = left + colWidth;
            float startY = paddingTop + colHeight / 2;
            startY -= colHeight / 2 * levels[i];
            if (startY < paddingTop) {
                startY = paddingTop;
            }
            float bottom = viewHeight - paddingBottom;
            RectF rect = new RectF(left, startY, right, bottom);
            canvas.drawRect(rect, mPaint);
        }
        mHandler.removeCallbacks(mRefreashRunnable);
        mHandler.postDelayed(mRefreashRunnable, mFrequency);
    }

    /**
     * Used to generate out the float array with specify array count
     *
     * @param count The array count
     * @return A float array with specify array count
     */
    private float[] generate(int count) {
        if (count <= 0) {
            return null;
        }
        int[] sign = {
                -1, 1
        };
        float[] result = new float[count];
        for (int i = 0; i < count; i++) {
            while (true) {
                result[i] = (float) Math.random() * 1f
                        * (float) sign[(int) (Math.random() * 2)];
                if (Math.abs(mPrevLevels[i] - result[i]) < 0.3f & result[i] > -0.3f) {
                    break;
                }
            }
        }
        mPrevLevels = result;
        return result;
    }
}
