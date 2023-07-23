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

package com.android.fmradio.views;

import android.animation.Animator;
import android.animation.Animator.AnimatorListener;
import android.animation.AnimatorListenerAdapter;
import android.animation.ObjectAnimator;
import android.content.Context;
import android.content.res.Configuration;
import android.content.res.Resources;
import android.content.res.TypedArray;
import android.database.Cursor;
import android.graphics.Canvas;
import android.graphics.Outline;
import android.graphics.Paint;
import android.graphics.PorterDuff;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.hardware.display.DisplayManagerGlobal;
import android.os.Handler;
import android.os.Looper;
import android.util.AttributeSet;
import android.util.DisplayMetrics;
import android.util.TypedValue;
import android.view.ContextThemeWrapper;
import android.view.Display;
import android.view.DisplayInfo;
import android.view.LayoutInflater;
import android.view.Menu;
import android.view.MenuItem;
import android.view.MotionEvent;
import android.view.VelocityTracker;
import android.view.View;
import android.view.ViewConfiguration;
import android.view.ViewGroup;
import android.view.ViewOutlineProvider;
import android.view.ViewTreeObserver.OnScrollChangedListener;
import android.view.ViewTreeObserver.OnPreDrawListener;
import android.view.animation.Interpolator;
import android.widget.AdapterView;
import android.widget.AdapterView.OnItemClickListener;
import android.widget.BaseAdapter;
import android.widget.EdgeEffect;
import android.widget.FrameLayout;
import android.widget.GridView;
import android.widget.ImageView;
import android.widget.PopupMenu;
import android.widget.PopupMenu.OnMenuItemClickListener;
import android.widget.RelativeLayout;
import android.widget.ScrollView;
import android.widget.Scroller;
import android.widget.TextView;

import com.android.fmradio.FmStation;
import com.android.fmradio.FmUtils;
import com.android.fmradio.Utils;
import com.android.fmradio.R;
import com.android.fmradio.FmStation.Station;

import android.support.v7.widget.CardView;

/**
 * Modified from Contact MultiShrinkScroll Handle the touch event and change
 * header size and scroll
 */
public class FmScroller extends FrameLayout {
    private static final String TAG = "FmScroller";

    /**
     * 1000 pixels per millisecond. Ie, 1 pixel per second.
     */
    private static final int PIXELS_PER_SECOND = 1000;
    private static final int ON_PLAY_ANIMATION_DELAY = 1000;
    private static final int PORT_COLUMN_NUM = 3;
    private static final int LAND_COLUMN_NUM = 5;
    private static final int STATE_NO_FAVORITE = 0;
    private static final int STATE_HAS_FAVORITE = 1;

    private float[] mLastEventPosition = {
            0, 0
    };
    private VelocityTracker mVelocityTracker;
    private boolean mIsBeingDragged = false;
    private boolean mReceivedDown = false;
    private boolean mFirstOnResume = true;

    private String mSelection = "IS_FAVORITE=?";
    private String[] mSelectionArgs = {
        "1"
    };

    private EventListener mEventListener;
    private PopupMenu mPopupMenu;
    private Handler mMainHandler;
    private ScrollView mScrollView;
    private View mScrollViewChild;
    private GridView mGridView;
    private TextView mFavoriteText;
    private View mHeader;
    private int mMaximumHeaderHeight;
    private int mMinimumHeaderHeight;
    private Adjuster mAdjuster;
    private int mCurrentStation;
    private boolean mIsFmPlaying;

    ViewOutlineProvider mViewOutlineProvider = new ViewOutlineProvider() {
        @Override
        public void getOutline(final View view, final Outline outline) {
            float cornerRadiusDP = 28f;
            float cornerRadius =
                TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP,
                        cornerRadiusDP, getResources().getDisplayMetrics());
            outline.setRoundRect(0, 0, view.getWidth(), (int) (view.getHeight() + cornerRadius), cornerRadius);
        }
    };

    private FavoriteAdapter mAdapter;
    private final Scroller mScroller;
    private final EdgeEffect mEdgeGlowBottom;
    private final int mTouchSlop;
    private final int mMaximumVelocity;
    private final int mMinimumVelocity;
    private final int mActionBarSize;

    private final AnimatorListener mHeaderExpandAnimationListener = new AnimatorListenerAdapter() {
        @Override
        public void onAnimationEnd(Animator animation) {
            refreshStateHeight();
        }
    };

    /**
     * Interpolator from android.support.v4.view.ViewPager. Snappier and more
     * elastic feeling than the default interpolator.
     */
    private static final Interpolator INTERPOLATOR = new Interpolator() {

        /**
         * {@inheritDoc}
         */
        @Override
        public float getInterpolation(float t) {
            t -= 1.0f;
            return t * t * t * t * t + 1.0f;
        }
    };

    /**
     * Constructor
     *
     * @param context The context
     */
    public FmScroller(Context context) {
        this(context, null);
    }

    /**
     * Constructor
     *
     * @param context The context
     * @param attrs The attrs
     */
    public FmScroller(Context context, AttributeSet attrs) {
        this(context, attrs, 0);
    }

    /**
     * Constructor
     *
     * @param context The context
     * @param attrs The attrs
     * @param defStyleAttr The default attr
     */
    public FmScroller(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);

        final ViewConfiguration configuration = ViewConfiguration.get(context);
        setFocusable(false);

        // Drawing must be enabled in order to support EdgeEffect
        setWillNotDraw(/* willNotDraw = */false);

        mEdgeGlowBottom = new EdgeEffect(context);
        mScroller = new Scroller(context, INTERPOLATOR);
        mTouchSlop = configuration.getScaledTouchSlop();
        mMinimumVelocity = configuration.getScaledMinimumFlingVelocity();
        mMaximumVelocity = configuration.getScaledMaximumFlingVelocity();

        final TypedArray attributeArray = context.obtainStyledAttributes(new int[] {
            android.R.attr.actionBarSize
        });
        mActionBarSize = attributeArray.getDimensionPixelSize(0, 0);
        attributeArray.recycle();
    }

    /**
     * This method must be called inside the Activity's OnCreate.
     */
    public void initialize() {
        mScrollView = (ScrollView) findViewById(R.id.content_scroller);

        mScrollView.setOutlineProvider(mViewOutlineProvider);
        mScrollView.setClipToOutline(true);

        int scrollViewBgColor = getContext().getColor(R.color.fav_container_bg_color);
        scrollViewBgColor = Utils.setColorAlphaComponent(scrollViewBgColor, 90);
        GradientDrawable bg = (GradientDrawable) mScrollView.getBackground();
        bg.setColor(scrollViewBgColor);

        mScrollView.getViewTreeObserver()
            .addOnScrollChangedListener(new OnScrollChangedListener() {
                @Override
                public void onScrollChanged() {
                    // Expand header once nested scroll view reaches top
                    if (!mScrollView.canScrollVertically(-1)) {
                        refreshStateHeight();
                        expandHeader();
                    }
                }
            });

        mScrollViewChild = findViewById(R.id.favorite_container);
        mHeader = findViewById(R.id.main_header_parent);

        int headerBgColor = getContext().getColor(R.color.header_bg_color);
        headerBgColor = Utils.setColorAlphaComponent(headerBgColor, 80);
        mHeader.setBackgroundColor(headerBgColor);

        mMainHandler = new Handler(Looper.getMainLooper());

        mFavoriteText = (TextView) findViewById(R.id.favorite_text);
        mGridView = (GridView) findViewById(R.id.gridview);
        mAdapter = new FavoriteAdapter(getContext());

        mAdjuster = new Adjuster(getContext());

        mGridView.setAdapter(mAdapter);
        Cursor c = getData();
        mAdapter.swipResult(c);
        mGridView.setFocusable(false);
        mGridView.setFocusableInTouchMode(false);

        mGridView.setOnItemClickListener(new OnItemClickListener() {

            @Override
            public void onItemClick(AdapterView<?> parent, View view, int position, long id) {
                if (mEventListener != null && mAdapter != null) {
                    mEventListener.onPlay(mAdapter.getFrequency(position));
                }

                mMainHandler.removeCallbacks(null);
                mMainHandler.postDelayed(new Runnable() {
                    @Override
                    public void run() {
                        mMaximumHeaderHeight = getMaxHeight(STATE_HAS_FAVORITE);
                        expandHeader();
                    }
                }, ON_PLAY_ANIMATION_DELAY);

            }
        });

        // Called when first time create activity
        doOnPreDraw(this, /* drawNextFrame = */false, new Runnable() {
            @Override
            public void run() {
                refreshStateHeight();
                setHeaderHeight(getMaximumScrollableHeaderHeight());
                updateHeaderTextAndButton();
                refreshFavoriteLayout();
            }
        });
    }

    /**
     * Runs a piece of code just before the next draw, after layout and measurement
     *
     * @param view The view depend on
     * @param drawNextFrame Whether to draw next frame
     * @param runnable The executed runnable instance
     */
    private void doOnPreDraw(final View view, final boolean drawNextFrame,
            final Runnable runnable) {
        final OnPreDrawListener listener = new OnPreDrawListener() {
            @Override
            public boolean onPreDraw() {
                view.getViewTreeObserver().removeOnPreDrawListener(this);
                runnable.run();
                return drawNextFrame;
            }
        };
        view.getViewTreeObserver().addOnPreDrawListener(listener);
    }

    private void refreshFavoriteLayout() {
        setFavoriteTextHeight(mAdapter.getCount() == 0);
        setGridViewHeight(computeGridViewHeight());
    }

    private void setFavoriteTextHeight(boolean show) {
        if (mAdapter.getCount() == 0) {
            mFavoriteText.setVisibility(View.GONE);
        } else {
            mFavoriteText.setVisibility(View.VISIBLE);
        }
    }

    private void setGridViewHeight(int height) {
        final ViewGroup.LayoutParams params = mGridView.getLayoutParams();
        params.height = height;
        mGridView.setLayoutParams(params);
    }

    private int computeGridViewHeight() {
        int itemcount = mAdapter.getCount();
        if (itemcount == 0) {
            return 0;
        }
        int curOrientation = getResources().getConfiguration().orientation;
        final boolean isLandscape = curOrientation == Configuration.ORIENTATION_LANDSCAPE;
        int columnNum = isLandscape ? LAND_COLUMN_NUM : PORT_COLUMN_NUM;
        int itemHeight = (int) getResources().getDimension(R.dimen.fm_gridview_item_height);
        int itemPadding = (int) getResources().getDimension(R.dimen.fm_gridview_item_padding);
        int rownum = (int) Math.ceil(itemcount / (float) columnNum);
        int totalHeight = rownum * itemHeight + rownum * itemPadding;
        if (rownum == 2) {
            int minGridViewHeight = getHeight() - getMinHeight(STATE_HAS_FAVORITE) - 72;
            totalHeight = Math.max(totalHeight, minGridViewHeight);
        }

        return totalHeight;
    }

    @Override
    public boolean onInterceptTouchEvent(MotionEvent event) {
        // The only time we want to intercept touch events is when we are being
        // dragged.
        return shouldStartDrag(event);
    }

    private boolean shouldStartDrag(MotionEvent event) {
        if (mIsBeingDragged) {
            mIsBeingDragged = false;
            return false;
        }

        switch (event.getAction()) {
        // If we are in the middle of a fling and there is a down event,
        // we'll steal it and
        // start a drag.
            case MotionEvent.ACTION_DOWN:
                updateLastEventPosition(event);
                if (!mScroller.isFinished()) {
                    startDrag();
                    return true;
                } else {
                    mReceivedDown = true;
                }
                break;

            // Otherwise, we will start a drag if there is enough motion in the
            // direction we are
            // capable of scrolling.
            case MotionEvent.ACTION_MOVE:
                if (motionShouldStartDrag(event)) {
                    updateLastEventPosition(event);
                    startDrag();
                    return true;
                }
                break;

            default:
                break;
        }

        return false;
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        final int action = event.getAction();

        if (mVelocityTracker == null) {
            mVelocityTracker = VelocityTracker.obtain();
        }
        mVelocityTracker.addMovement(event);
        if (!mIsBeingDragged) {
            if (shouldStartDrag(event)) {
                return true;
            }

            if (action == MotionEvent.ACTION_UP && mReceivedDown) {
                mReceivedDown = false;
                return performClick();
            }
            return true;
        }

        switch (action) {
            case MotionEvent.ACTION_MOVE:
                final float delta = updatePositionAndComputeDelta(event);
                scrollTo(0, getScroll() + (int) delta);
                mReceivedDown = false;

                if (mIsBeingDragged) {
                    final int distanceFromMaxScrolling = getMaximumScrollUpwards() - getScroll();
                    if (delta > distanceFromMaxScrolling) {
                        // The ScrollView is being pulled upwards while there is
                        // no more
                        // content offscreen, and the view port is already fully
                        // expanded.
                        mEdgeGlowBottom.onPull(delta / getHeight(), 1 - event.getX() / getWidth());
                    }

                    if (!mEdgeGlowBottom.isFinished()) {
                        postInvalidateOnAnimation();
                    }

                }
                break;

            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                stopDrag(action == MotionEvent.ACTION_CANCEL);
                mReceivedDown = false;
                break;

            default:
                break;
        }

        return true;
    }

    /**
     * Expand to maximum size or starting size. Disable clicks on the
     * photo until the animation is complete.
     */
    private void expandHeader() {
        if (getHeaderHeight() != mMaximumHeaderHeight) {
            // Expand header
            final ObjectAnimator animator = ObjectAnimator.ofInt(this, "headerHeight",
                    mMaximumHeaderHeight);
            animator.addListener(mHeaderExpandAnimationListener);
            animator.setDuration(300);
            animator.start();
            // Scroll nested scroll view to its top
            if (mScrollView.getScrollY() != 0) {
                ObjectAnimator.ofInt(mScrollView, "scrollY", 0).setDuration(300).start();
            }
        }
    }

    private void collapseHeader() {
        if (getHeaderHeight() != mMinimumHeaderHeight) {
            final ObjectAnimator animator = ObjectAnimator.ofInt(this, "headerHeight",
                    mMinimumHeaderHeight);
            animator.addListener(mHeaderExpandAnimationListener);
            animator.start();
        }
    }

    private void startDrag() {
        mIsBeingDragged = true;
        mScroller.abortAnimation();
    }

    private void stopDrag(boolean cancelled) {
        mIsBeingDragged = false;
        if (!cancelled && getChildCount() > 0) {
            final float velocity = getCurrentVelocity();
            if (velocity > mMinimumVelocity || velocity < -mMinimumVelocity) {
                fling(-velocity);
            }
        }

        if (mVelocityTracker != null) {
            mVelocityTracker.recycle();
            mVelocityTracker = null;
        }

        mEdgeGlowBottom.onRelease();
    }

    @Override
    public void scrollTo(int x, int y) {
        final int delta = y - getScroll();
        if (delta > 0) {
            scrollUp(delta);
        } else {
            scrollDown(delta);
        }
        updateHeaderTextAndButton();
    }

    private int getToolbarHeight() {
        return mHeader.getLayoutParams().height;
    }

    /**
     * Set the height of the toolbar and update its tint accordingly.
     */
    @FmReflection
    public void setHeaderHeight(int height) {
        final ViewGroup.LayoutParams toolbarLayoutParams = mHeader.getLayoutParams();
        toolbarLayoutParams.height = height;
        mHeader.setLayoutParams(toolbarLayoutParams);
        updateHeaderTextAndButton();
    }

    /**
     * Get header height. Used in ObjectAnimator
     *
     * @return The header height
     */
    @FmReflection
    public int getHeaderHeight() {
        return mHeader.getLayoutParams().height;
    }

    /**
     * Set scroll. Used in ObjectAnimator
     */
    @FmReflection
    public void setScroll(int scroll) {
        scrollTo(0, scroll);
    }

    /**
     * Returns the total amount scrolled inside the nested ScrollView + the amount
     * of shrinking performed on the ToolBar. This is the value inspected by animators.
     */
    @FmReflection
    public int getScroll() {
        return getMaximumScrollableHeaderHeight() - getToolbarHeight() + mScrollView.getScrollY();
    }

    private int getMaximumScrollableHeaderHeight() {
        return mMaximumHeaderHeight;
    }

    /**
     * A variant of {@link #getScroll} that pretends the header is never
     * larger than than mIntermediateHeaderHeight. This function is sometimes
     * needed when making scrolling decisions that will not change the header
     * size (ie, snapping to the bottom or top). When mIsOpenContactSquare is
     * true, this function considers mIntermediateHeaderHeight == mMaximumHeaderHeight,
     * since snapping decisions will be made relative the full header size when
     * mIsOpenContactSquare = true. This value should never be used in conjunction
     * with {@link #getScroll} values.
     */
    private int getScrollIgnoreOversizedHeaderForSnapping() {
        return Math.max(getMaximumScrollableHeaderHeight() - getToolbarHeight(), 0)
                + mScrollView.getScrollY();
    }

    /**
     * Return amount of scrolling needed in order for all the visible
     * subviews to scroll off the bottom.
     */
    private int getScrollUntilOffBottom() {
        return getHeight() + getScrollIgnoreOversizedHeaderForSnapping();
    }

    @Override
    public void computeScroll() {
        if (mScroller.computeScrollOffset()) {
            // Examine the fling results in order to activate EdgeEffect when we
            // fling to the end.
            final int oldScroll = getScroll();
            scrollTo(0, mScroller.getCurrY());
            final int delta = mScroller.getCurrY() - oldScroll;
            final int distanceFromMaxScrolling = getMaximumScrollUpwards() - getScroll();
            if (delta > distanceFromMaxScrolling && distanceFromMaxScrolling > 0) {
                mEdgeGlowBottom.onAbsorb((int) mScroller.getCurrVelocity());
            }

            if (!awakenScrollBars()) {
                // Keep on drawing until the animation has finished.
                postInvalidateOnAnimation();
            }
            if (mScroller.getCurrY() >= getMaximumScrollUpwards()) {
                mScroller.abortAnimation();
            }
        }
    }

    @Override
    public void draw(Canvas canvas) {
        super.draw(canvas);

        if (!mEdgeGlowBottom.isFinished()) {
            final int restoreCount = canvas.save();
            final int width = getWidth() - getPaddingLeft() - getPaddingRight();
            final int height = getHeight();

            // Draw the EdgeEffect on the bottom of the Window (Or a little bit
            // below the bottom
            // of the Window if we start to scroll upwards while EdgeEffect is
            // visible). This
            // does not need to consider the case where this MultiShrinkScroller
            // doesn't fill
            // the Window, since the nested ScrollView should be set to
            // fillViewport.
            canvas.translate(-width + getPaddingLeft(), height + getMaximumScrollUpwards()
                    - getScroll());

            canvas.rotate(180, width, 0);
            mEdgeGlowBottom.setSize(width, height);
            if (mEdgeGlowBottom.draw(canvas)) {
                postInvalidateOnAnimation();
            }
            canvas.restoreToCount(restoreCount);
        }
    }

    private float getCurrentVelocity() {
        if (mVelocityTracker == null) {
            return 0;
        }
        mVelocityTracker.computeCurrentVelocity(PIXELS_PER_SECOND, mMaximumVelocity);
        return mVelocityTracker.getYVelocity();
    }

    private void fling(float velocity) {
        // For reasons I do not understand, scrolling is less janky when
        // maxY=Integer.MAX_VALUE
        // then when maxY is set to an actual value.
        mScroller.fling(0, getScroll(), 0, (int) velocity, 0, 0, -Integer.MAX_VALUE,
                Integer.MAX_VALUE);
        invalidate();
    }

    private int getMaximumScrollUpwards() {
        return // How much the Header view can compress
        getMaximumScrollableHeaderHeight() - getFullyCompressedHeaderHeight()
        // How much the ScrollView can scroll. 0, if child is
        // smaller than ScrollView.
                + Math.max(0, mScrollViewChild.getHeight() - getHeight()
                        + getFullyCompressedHeaderHeight());
    }

    private void scrollUp(int delta) {
        final ViewGroup.LayoutParams toolbarLayoutParams = mHeader.getLayoutParams();
        if (toolbarLayoutParams.height > getFullyCompressedHeaderHeight()) {
            final int originalValue = toolbarLayoutParams.height;
            toolbarLayoutParams.height -= delta;
            toolbarLayoutParams.height = Math.max(toolbarLayoutParams.height,
                    getFullyCompressedHeaderHeight());
            mHeader.setLayoutParams(toolbarLayoutParams);
            delta -= originalValue - toolbarLayoutParams.height;
        }
        mScrollView.scrollBy(0, delta);
    }

    /**
     * Returns the minimum size that we want to compress the header to,
     * given that we don't want to allow the the ScrollView to scroll
     * unless there is new content off of the edge of ScrollView.
     */
    private int getFullyCompressedHeaderHeight() {
        int height = Math.min(Math.max(mHeader.getLayoutParams().height
                - getOverflowingChildViewSize(), mMinimumHeaderHeight),
                getMaximumScrollableHeaderHeight());
        return height;
    }

    /**
     * Returns the amount of mScrollViewChild that doesn't fit inside its parent. Outside size
     */
    private int getOverflowingChildViewSize() {
        final int usedScrollViewSpace = mScrollViewChild.getHeight();
        return -getHeight() + usedScrollViewSpace + mHeader.getLayoutParams().height;
    }

    private void scrollDown(int delta) {
        if (mScrollView.getScrollY() > 0) {
            final int originalValue = mScrollView.getScrollY();
            mScrollView.scrollBy(0, delta);
        }
    }

    private void updateHeaderTextAndButton() {
        mAdjuster.handleScroll();
    }

    private void updateLastEventPosition(MotionEvent event) {
        mLastEventPosition[0] = event.getX();
        mLastEventPosition[1] = event.getY();
    }

    private boolean motionShouldStartDrag(MotionEvent event) {
        final float deltaX = event.getX() - mLastEventPosition[0];
        final float deltaY = event.getY() - mLastEventPosition[1];
        final boolean draggedX = (deltaX > mTouchSlop || deltaX < -mTouchSlop);
        final boolean draggedY = (deltaY > mTouchSlop || deltaY < -mTouchSlop);
        return draggedY && !draggedX;
    }

    private float updatePositionAndComputeDelta(MotionEvent event) {
        final int vertical = 1;
        final float position = mLastEventPosition[vertical];
        updateLastEventPosition(event);
        return position - mLastEventPosition[vertical];
    }

    /**
     * Interpolator that enforces a specific starting velocity.
     * This is useful to avoid a discontinuity between dragging
     * speed and flinging speed. Similar to a
     * {@link android.view.animation.AccelerateInterpolator} in
     * the sense that getInterpolation() is a quadratic function.
     */
    private static class AcceleratingFlingInterpolator implements Interpolator {

        private final float mStartingSpeedPixelsPerFrame;

        private final float mDurationMs;

        private final int mPixelsDelta;

        private final float mNumberFrames;

        public AcceleratingFlingInterpolator(int durationMs, float startingSpeedPixelsPerSecond,
                int pixelsDelta) {
            mStartingSpeedPixelsPerFrame = startingSpeedPixelsPerSecond / getRefreshRate();
            mDurationMs = durationMs;
            mPixelsDelta = pixelsDelta;
            mNumberFrames = mDurationMs / getFrameIntervalMs();
        }

        @Override
        public float getInterpolation(float input) {
            final float animationIntervalNumber = mNumberFrames * input;
            final float linearDelta = (animationIntervalNumber * mStartingSpeedPixelsPerFrame)
                    / mPixelsDelta;
            // Add the results of a linear interpolator (with the initial speed)
            // with the
            // results of a AccelerateInterpolator.
            if (mStartingSpeedPixelsPerFrame > 0) {
                return Math.min(input * input + linearDelta, 1);
            } else {
                // Initial fling was in the wrong direction, make sure that the
                // quadratic component
                // grows faster in order to make up for this.
                return Math.min(input * (input - linearDelta) + linearDelta, 1);
            }
        }

        private float getRefreshRate() {
            DisplayInfo di = DisplayManagerGlobal.getInstance().getDisplayInfo(
                    Display.DEFAULT_DISPLAY);
            return di.getMode().getRefreshRate();
        }

        public long getFrameIntervalMs() {
            return (long) (1000 / getRefreshRate());
        }
    }

    private int getMaxHeight(int state) {
        int height = 0;
        switch (state) {
            case STATE_NO_FAVORITE:
                height = getHeight();
                break;
            case STATE_HAS_FAVORITE:
                height = (int) getResources().getDimension(R.dimen.fm_main_header_big);
                break;
            default:
                break;
        }
        return height;
    }

    private int getMinHeight(int state) {
        int height = 0;
        switch (state) {
            case STATE_NO_FAVORITE:
                height = (int) getResources().getDimension(R.dimen.fm_main_header_big);
                break;
            case STATE_HAS_FAVORITE:
                height = (int) getResources().getDimension(R.dimen.fm_main_header_small);
                break;
            default:
                break;
        }
        return height;
    }

    private void setMinHeight(int height) {
        mMinimumHeaderHeight = height;
    }

    class FavoriteAdapter extends BaseAdapter {
        private Cursor mCursor;
        private int mCurrentPlayingPosition = -1;
        private LayoutInflater mInflater;
        private Context mContext;

        public FavoriteAdapter(Context context) {
            mInflater = LayoutInflater.from(context);
            mContext = context;
        }

        public int getFrequency(int position) {
            if (mCursor != null && mCursor.moveToFirst()) {
                mCursor.moveToPosition(position);
                return mCursor.getInt(mCursor.getColumnIndex(FmStation.Station.FREQUENCY));
            }
            return 0;
        }

        public void swipResult(Cursor cursor) {
            swipResult(cursor, true);
        }

        public void swipResult(Cursor cursor, boolean notify) {
            if (null != mCursor) {
                mCursor.close();
            }
            mCursor = cursor;
            if (notify) {
                notifyDataSetChanged();
            }
        }

        public void updateRDSViews(final TextView freqView, final TextView nameView,
                final TextView rtView, final String name, final String rt) {

            if (!rt.equals("")) {
                if (rtView.getVisibility() == View.GONE) {
                    rtView.setVisibility(View.VISIBLE);
                    rtView.setSelected(true);
                    // Move frequency to right of FM label
                    final RelativeLayout.LayoutParams params = new
                        RelativeLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
                                ViewGroup.LayoutParams.WRAP_CONTENT);
                    params.addRule(RelativeLayout.RIGHT_OF, R.id.fm_label);
                    freqView.setLayoutParams(params);
                    freqView.setTypeface(Typeface.create("sans-serif", Typeface.NORMAL));

                    final float paddingLeftDp = 3f;
                    final float paddingLeftDpPx =
                        TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP,
                                paddingLeftDp, mContext.getResources().getDisplayMetrics());
                    freqView.setPadding((int) paddingLeftDpPx,
                            freqView.getPaddingTop(),
                            freqView.getPaddingRight(),
                            freqView.getPaddingBottom());
                    final float textSizeDp = 14f;
                    freqView.setTextSize(TypedValue.COMPLEX_UNIT_DIP, textSizeDp);
                }

                final String old_rt = rtView.getText().toString();
                if (0 != rt.compareTo(old_rt)) {
                    rtView.setText(rt);
                }
            } else {
                rtView.setVisibility(View.GONE);
                // Move frequency below FM label
                final RelativeLayout.LayoutParams params = new
                    RelativeLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
                            ViewGroup.LayoutParams.WRAP_CONTENT);
                params.addRule(RelativeLayout.BELOW, R.id.fm_label);
                freqView.setLayoutParams(params);
                freqView.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
                freqView.setPadding(0, freqView.getPaddingTop(),
                        freqView.getPaddingRight(),
                        freqView.getPaddingBottom());
                final float textSizeDp = 18f;
                freqView.setTextSize(TypedValue.COMPLEX_UNIT_DIP, textSizeDp);
            }


            if ("".equals(name) && rt.equals("")) {
                nameView.setVisibility(View.GONE);
            } else {
                if (nameView.getVisibility() == View.GONE) {
                    nameView.setVisibility(View.VISIBLE);
                    nameView.setSelected(true);
                }
                final String old_name = nameView.getText().toString();
                if (0 != name.compareTo(old_name)) {
                    nameView.setText(name);
                }
            }
        }

        public int getCurrentPlayingPosition() {
            return mCurrentPlayingPosition;
        }

        @Override
        public int getCount() {
            if (null != mCursor) {
                return mCursor.getCount();
            }
            return 0;
        }

        @Override
        public Object getItem(int position) {
            return null;
        }

        @Override
        public long getItemId(int position) {
            return 0;
        }

        @Override
        public View getView(int position, View convertView, ViewGroup parent) {
            ViewHolder viewHolder = null;
            if (null == convertView) {
                viewHolder = new ViewHolder();
                convertView = mInflater.inflate(R.layout.favorite_gridview_item, null);

                int cardBgColor = getContext().getColor(R.color.favorite_tile_bg_color);
                cardBgColor = Utils.setColorAlphaComponent(cardBgColor, 60);
                viewHolder.mCardView = (CardView) convertView.findViewById(R.id.card_view);
                viewHolder.mCardView.setCardBackgroundColor(cardBgColor);

                viewHolder.mFmLabel = (TextView) convertView.findViewById(R.id.fm_label);
                viewHolder.mStationFreq = (TextView) convertView.findViewById(R.id.station_freq);
                viewHolder.mPlayIndicator = (FmVisualizerView) convertView
                        .findViewById(R.id.fm_play_indicator);
                viewHolder.mStationName = (TextView) convertView.findViewById(R.id.station_name);
                viewHolder.mStationRt = (TextView) convertView.findViewById(R.id.station_rt);
                viewHolder.mMoreButton = (ImageView) convertView.findViewById(R.id.station_more);
                viewHolder.mPopupMenuAnchor = convertView.findViewById(R.id.popupmenu_anchor);
                convertView.setTag(viewHolder);
            } else {
                viewHolder = (ViewHolder) convertView.getTag();
            }

            if (mCursor != null && mCursor.moveToPosition(position)) {
                final int stationFreq = mCursor.getInt(mCursor
                        .getColumnIndex(FmStation.Station.FREQUENCY));
                String name = mCursor.getString(mCursor
                        .getColumnIndex(FmStation.Station.STATION_NAME));
                String rt = mCursor.getString(mCursor
                        .getColumnIndex(FmStation.Station.RADIO_TEXT));
                final int isFavorite = mCursor.getInt(mCursor
                        .getColumnIndex(FmStation.Station.IS_FAVORITE));

                if (mCurrentStation == stationFreq) {
                    mCurrentPlayingPosition = position;
                }

                if (null == name || "".equals(name)) {
                    name = mCursor.getString(mCursor
                            .getColumnIndex(FmStation.Station.PROGRAM_SERVICE));
                }
                if (null == name) {
                    name = "";
                }
                if (null == rt) {
                    rt = "";
                }

                viewHolder.mStationFreq.setText(FmUtils.formatStation(stationFreq));
                updateRDSViews(viewHolder.mStationFreq,
                        viewHolder.mStationName, viewHolder.mStationRt,
                        name, rt);

                int fmLabelColorId, stationFreqColorId, stationNameColorId,
                    moreButtonAccentColorId;
                if (mCurrentStation == stationFreq) {
                    viewHolder.mPlayIndicator.setVisibility(View.VISIBLE);
                    if (mIsFmPlaying) {
                        viewHolder.mPlayIndicator.startAnimation();
                    } else {
                        viewHolder.mPlayIndicator.stopAnimation();
                    }
                    fmLabelColorId = stationFreqColorId =
                    stationNameColorId =
                        R.color.favorite_station_accent_playing_color;
                    moreButtonAccentColorId =
                        R.color.favorite_station_more_accent_playing_color;
                } else {
                    viewHolder.mPlayIndicator.setVisibility(View.GONE);
                    viewHolder.mPlayIndicator.stopAnimation();
                    fmLabelColorId = R.color.favorite_fm_label_color;
                    stationFreqColorId = R.color.favorite_station_freq_color;
                    stationNameColorId = R.color.favorite_station_name_color;
                    moreButtonAccentColorId = R.color.favorite_station_more_accent_color;
                }
                Resources r = getResources();
                viewHolder.mFmLabel.setTextColor(r.getColor(fmLabelColorId));
                viewHolder.mStationFreq.setTextColor(r.getColor(stationFreqColorId));
                viewHolder.mStationName.setTextColor(r.getColor(stationNameColorId));
                viewHolder.mStationRt.setTextColor(r.getColor(stationNameColorId));
                viewHolder.mMoreButton.setColorFilter(r.getColor(moreButtonAccentColorId),
                        PorterDuff.Mode.SRC_ATOP);
                int moreButtonBgColor = Utils
                    .setColorAlphaComponent(r.getColor(moreButtonAccentColorId), 10);
                viewHolder.mMoreButton
                    .getBackground().setTint(moreButtonBgColor);
                viewHolder.mMoreButton.setTag(viewHolder.mPopupMenuAnchor);
                viewHolder.mMoreButton.setOnClickListener(new OnClickListener() {
                    @Override
                    public void onClick(View v) {
                        // Use anchor view to fix PopupMenu postion and cover more button
                        View anchor = v;
                        if (v.getTag() != null) {
                            anchor = (View) v.getTag();
                        }
                        showPopupMenu(anchor, stationFreq);
                    }
                });
            }

            return convertView;
        }
    }

    private Cursor getData() {
        Cursor cursor = getContext().getContentResolver().query(Station.CONTENT_URI,
                FmStation.COLUMNS, mSelection, mSelectionArgs,
                FmStation.Station.FREQUENCY);
        return cursor;
    }

    /**
     * Called when FmRadioActivity.onResume(), refresh layout
     */
    public void onResume() {
        Cursor c = getData();
        mAdapter.swipResult(c);
        if (mFirstOnResume) {
            mFirstOnResume = false;
        } else {
            refreshStateHeight();
            updateHeaderTextAndButton();
            refreshFavoriteLayout();

            int curOrientation = getResources().getConfiguration().orientation;
            final boolean isLandscape = curOrientation == Configuration.ORIENTATION_LANDSCAPE;
            int columnNum = isLandscape ? LAND_COLUMN_NUM : PORT_COLUMN_NUM;
            boolean isOneRow = c.getCount() <= columnNum;

            boolean hasFavoriteCurrent = c.getCount() > 0;
            if (mHasFavoriteWhenOnPause != hasFavoriteCurrent || isOneRow) {
                setHeaderHeight(getMaximumScrollableHeaderHeight());
            }
        }
    }

    private boolean mHasFavoriteWhenOnPause = false;

    /**
     * Called when FmRadioActivity.onPause()
     */
    public void onPause() {
        if (mAdapter != null && mAdapter.getCount() > 0) {
            mHasFavoriteWhenOnPause = true;
        } else {
            mHasFavoriteWhenOnPause = false;
        }
    }

    /**
     * Notify refresh adapter when data change
     */
    public void notifyAdapterChange() {
        Cursor c = getData();
        mAdapter.swipResult(c);
    }

    /**
     * Selectively update RDS infos and passively refresh adapter without
     * triggering full re-rendering
     */
    public void notifyAdatperCurrentItemRDSChanged() {
        int pos = mAdapter.getCurrentPlayingPosition();
        if (pos < 0) {
            return;
        }
        View v = mGridView.getChildAt(pos);
        Cursor c = getData();
        if (c != null && c.moveToPosition(pos)) {
            String name =
                c.getString(c.getColumnIndex(FmStation.Station.STATION_NAME));
            String rt =
                c.getString(c.getColumnIndex(FmStation.Station.RADIO_TEXT));
            if (null == name || "".equals(name)) {
                name =
                    c.getString(c.getColumnIndex(FmStation.Station.PROGRAM_SERVICE));
            }
            if (null == name) {
                name = "";
            }
            if (null == rt) {
                rt = "";
            }

            TextView freqView = (TextView) v.findViewById(R.id.station_freq);
            TextView nameView = (TextView) v.findViewById(R.id.station_name);
            TextView rtView = (TextView) v.findViewById(R.id.station_rt);
            mAdapter.updateRDSViews(freqView, nameView, rtView, name, rt);

            mAdapter.swipResult(c, false);
        }
    }

    private void refreshStateHeight() {
        if (mAdapter != null && mAdapter.getCount() > 0) {
            mMaximumHeaderHeight = getMaxHeight(STATE_HAS_FAVORITE);
            mMinimumHeaderHeight = getMinHeight(STATE_HAS_FAVORITE);
        } else {
            mMaximumHeaderHeight = getMaxHeight(STATE_NO_FAVORITE);
            mMinimumHeaderHeight = getMinHeight(STATE_NO_FAVORITE);
        }
    }

    /**
     * Called when add a favorite
     */
    public void onAddFavorite() {
        Cursor c = getData();
        mAdapter.swipResult(c);
        refreshFavoriteLayout();
        if (c.getCount() == 1) {
            // Last time count is 0, so need set STATE_NO_FAVORITE then collapse header
            mMinimumHeaderHeight = getMinHeight(STATE_NO_FAVORITE);
            mMaximumHeaderHeight = getMaxHeight(STATE_NO_FAVORITE);
            collapseHeader();
        }
    }

    /**
     * Called when remove a favorite
     */
    public void onRemoveFavorite() {
        Cursor c = getData();
        mAdapter.swipResult(c);
        refreshFavoriteLayout();
        if (c != null && c.getCount() == 0) {
            // Stop the play animation
            mMainHandler.removeCallbacks(null);

            // Last time count is 1, so need set STATE_NO_FAVORITE then expand header
            mMinimumHeaderHeight = getMinHeight(STATE_NO_FAVORITE);
            mMaximumHeaderHeight = getMaxHeight(STATE_NO_FAVORITE);
            expandHeader();
        }
    }

    private void showPopupMenu(View anchor, final int frequency) {
        dismissPopupMenu();
        Context wrapper = new ContextThemeWrapper(getContext(), R.style.AppThemeMain_FavoriteTilePopupStyle);
        mPopupMenu = new PopupMenu(wrapper, anchor);
        Menu menu = mPopupMenu.getMenu();
        mPopupMenu.getMenuInflater().inflate(R.menu.gridview_item_more_menu, menu);
        mPopupMenu.setOnMenuItemClickListener(new OnMenuItemClickListener() {
            @Override
            public boolean onMenuItemClick(MenuItem item) {
                switch (item.getItemId()) {
                    case R.id.remove_favorite:
                        if (mEventListener != null) {
                            mEventListener.onRemoveFavorite(frequency);
                        }
                        break;
                    case R.id.rename:
                        if (mEventListener != null) {
                            mEventListener.onRename(frequency);
                        }
                        break;
                    default:
                        break;
                }
                return false;
            }
        });
        mPopupMenu.show();
    }

    private void dismissPopupMenu() {
        if (mPopupMenu != null) {
            mPopupMenu.dismiss();
            mPopupMenu = null;
        }
    }

    /**
     * Called when FmRadioActivity.onDestory()
     */
    public void closeAdapterCursor() {
        mAdapter.swipResult(null);
    }

    /**
     * Register a listener for GridView item event
     *
     * @param listener The event listener
     */
    public void registerListener(EventListener listener) {
        mEventListener = listener;
    }

    /**
     * Unregister a listener for GridView item event
     *
     * @param listener The event listener
     */
    public void unregisterListener(EventListener listener) {
        mEventListener = null;
    }

    /**
     * Listen for GridView item event: remove, rename, click play
     */
    public interface EventListener {
        /**
         * Callback when click remove favorite menu
         *
         * @param frequency The frequency want to remove
         */
        void onRemoveFavorite(int frequency);

        /**
         * Callback when click rename favorite menu
         *
         * @param frequency The frequency want to rename
         */
        void onRename(int frequency);

        /**
         * Callback when click gridview item to play
         *
         * @param frequency The frequency want to play
         */
        void onPlay(int frequency);
    }

    /**
     * Refresh the play indicator in gridview when play station or play state change
     *
     * @param currentStation current station
     * @param isFmPlaying whether fm is playing
     */
    public void refreshPlayIndicator(int currentStation, boolean isFmPlaying) {
        mCurrentStation = currentStation;
        mIsFmPlaying = isFmPlaying;
        if (mAdapter != null) {
            mAdapter.notifyDataSetChanged();
        }
    }

    /**
     * Adjust view padding and text size when scroll
     */
    private class Adjuster {
        private final DisplayMetrics mDisplayMetrics;

        private final int mFirstTargetHeight;

        private final int mSecondTargetHeight;

        private final int mActionBarHeight = mActionBarSize;

        private final int mStatusBarHeight;

        private final int mFullHeight;// display height without status bar

        private final float mDensity;

        private final Typeface mDefaultFrequencyTypeface;

        // Text view
        private TextView mFrequencyText;

        private TextView mFmDescriptionText;

        private TextView mStationNameText;

        private TextView mStationRdsText;

        /*
         * The five control buttons view(previous, next, increase,
         * decrease, favorite) and stop button
         */
        private View mControlView;

        private View mPlayButtonView;

        private final Context mContext;

        private final boolean mIsLandscape;

        private FirstRangeAdjuster mFirstRangeAdjuster;

        private SecondRangeAdjuster mSecondRangeAdjusterr;

        public Adjuster(Context context) {
            mContext = context;
            mDisplayMetrics = mContext.getResources().getDisplayMetrics();
            mDensity = mDisplayMetrics.density;
            int curOrientation = getResources().getConfiguration().orientation;
            mIsLandscape = curOrientation == Configuration.ORIENTATION_LANDSCAPE;
            Resources res = mContext.getResources();
            mFirstTargetHeight = res.getDimensionPixelSize(R.dimen.fm_main_header_big);
            mSecondTargetHeight = res.getDimensionPixelSize(R.dimen.fm_main_header_small);
            mStatusBarHeight = res
                    .getDimensionPixelSize(com.android.internal.R.dimen.status_bar_height);
            mFullHeight = mDisplayMetrics.heightPixels - mStatusBarHeight;

            mFrequencyText = (TextView) findViewById(R.id.station_value);
            mFmDescriptionText = (TextView) findViewById(R.id.text_fm);
            mStationNameText = (TextView) findViewById(R.id.station_name);
            mStationRdsText = (TextView) findViewById(R.id.station_rds);
            mControlView = findViewById(R.id.rl_imgbtnpart);
            mPlayButtonView = findViewById(R.id.play_button_container);

            mFirstRangeAdjuster = new FirstRangeAdjuster();
            mSecondRangeAdjusterr = new SecondRangeAdjuster();
            mControlView.setMinimumWidth(mIsLandscape ? mDisplayMetrics.heightPixels
                    : mDisplayMetrics.widthPixels);
            mDefaultFrequencyTypeface = mFrequencyText.getTypeface();
        }

        public void handleScroll() {
            int height = getHeaderHeight();
            if (mIsLandscape || height > mFirstTargetHeight) {
                mFirstRangeAdjuster.handleScroll();
            } else if (height >= mSecondTargetHeight) {
                mSecondRangeAdjusterr.handleScroll();
            }
        }

        private class FirstRangeAdjuster {
            protected int mTargetHeight;

            // start text size and margin
            protected float mFmDescriptionTextSizeStart;

            protected float mFrequencyStartTextSize;

            protected float mStationNameTextSizeStart;

            protected float mFmDescriptionMarginTopStart;

            protected float mFmDescriptionStartPaddingLeft;

            protected float mFrequencyMarginTopStart;

            protected float mStationNameMarginTopStart;

            protected float mStationRdsMarginTopStart;

            protected float mControlViewMarginTopStart;

            // target text size and margin
            protected float mFmDescriptionTextSizeTarget;

            protected float mFrequencyTextSizeTarget;

            protected float mStationNameTextSizeTarget;

            protected float mFmDescriptionMarginTopTarget;

            protected float mFrequencyMarginTopTarget;

            protected float mStationNameMarginTopTarget;

            protected float mStationRdsMarginTopTarget;

            protected float mControlViewMarginTopTarget;

            protected float mPlayButtonMarginTopStart;

            protected float mPlayButtonMarginTopTarget;

            protected float mPlayButtonHeight;

            // Padding adjust rate as linear
            protected float mFmDescriptionPaddingRate;

            protected float mFrequencyPaddingRate;

            protected float mStationNamePaddingRate;

            protected float mStationRdsPaddingRate;

            protected float mControlViewPaddingRate;

            // init it with display height
            protected float mPlayButtonPaddingRate;

            // Text size adjust rate as linear
            // adjust from first to target critical height
            protected float mFmDescriptionTextSizeRate;

            protected float mFrequencyTextSizeRate;

            // adjust before first critical height
            protected float mStationNameTextSizeRate;

            public FirstRangeAdjuster() {
                Resources res = mContext.getResources();
                mTargetHeight = mFirstTargetHeight;
                // init start
                mFmDescriptionTextSizeStart = res.getDimension(R.dimen.fm_description_text_size);
                mFrequencyStartTextSize = res.getDimension(R.dimen.fm_frequency_text_size_start);
                mStationNameTextSizeStart = res
                        .getDimension(R.dimen.fm_station_name_text_size_start);
                // first view, margin refer to parent
                mFmDescriptionMarginTopStart = res
                        .getDimension(R.dimen.fm_description_margin_top_start) + mActionBarHeight;
                mFrequencyMarginTopStart = res.getDimension(R.dimen.fm_frequency_margin_top_start);
                mStationNameMarginTopStart = res
                        .getDimension(R.dimen.fm_station_name_margin_top_start);
                mStationRdsMarginTopStart = res
                        .getDimension(R.dimen.fm_station_rds_margin_top_start);
                mControlViewMarginTopStart = res
                        .getDimension(R.dimen.fm_control_buttons_margin_top_start);
                // init target
                mFrequencyTextSizeTarget = res
                        .getDimension(R.dimen.fm_frequency_text_size_first_target);
                mFmDescriptionTextSizeTarget = mFrequencyTextSizeTarget;
                mStationNameTextSizeTarget = res
                        .getDimension(R.dimen.fm_station_name_text_size_first_target);
                mFmDescriptionMarginTopTarget = res
                        .getDimension(R.dimen.fm_description_margin_top_first_target);
                mFmDescriptionStartPaddingLeft = mFrequencyText.getPaddingLeft();
                // first view, margin refer to parent if not in landscape
                if (!mIsLandscape) {
                    mFmDescriptionMarginTopTarget += mActionBarHeight;
                } else {
                    mFrequencyMarginTopStart += mActionBarHeight + mFmDescriptionTextSizeStart;
                }
                mFrequencyMarginTopTarget = res
                        .getDimension(R.dimen.fm_frequency_margin_top_first_target);
                mStationNameMarginTopTarget = res
                        .getDimension(R.dimen.fm_station_name_margin_top_first_target);
                mStationRdsMarginTopTarget = res
                        .getDimension(R.dimen.fm_station_rds_margin_top_first_target);
                mControlViewMarginTopTarget = res
                        .getDimension(R.dimen.fm_control_buttons_margin_top_first_target);
                // init text size and margin adjust rate
                int scrollHeight = mFullHeight - mTargetHeight;
                mFmDescriptionTextSizeRate =
                        (mFmDescriptionTextSizeStart - mFmDescriptionTextSizeTarget) / scrollHeight;
                mFrequencyTextSizeRate = (mFrequencyStartTextSize - mFrequencyTextSizeTarget)
                        / scrollHeight;
                mStationNameTextSizeRate = (mStationNameTextSizeStart - mStationNameTextSizeTarget)
                        / scrollHeight;
                mFmDescriptionPaddingRate =
                        (mFmDescriptionMarginTopStart - mFmDescriptionMarginTopTarget)
                        / scrollHeight;
                mFrequencyPaddingRate = (mFrequencyMarginTopStart - mFrequencyMarginTopTarget)
                        / scrollHeight;
                mStationNamePaddingRate = (mStationNameMarginTopStart - mStationNameMarginTopTarget)
                        / scrollHeight;
                mStationRdsPaddingRate = (mStationRdsMarginTopStart - mStationRdsMarginTopTarget)
                        / scrollHeight;
                mControlViewPaddingRate = (mControlViewMarginTopStart - mControlViewMarginTopTarget)
                        / scrollHeight;
                // init play button padding, it different to others, padding top refer to parent
                mPlayButtonHeight = res.getDimension(R.dimen.play_button_height);
                mPlayButtonMarginTopStart = mFullHeight - mPlayButtonHeight - 16 * mDensity;
                mPlayButtonMarginTopTarget = mFirstTargetHeight - mPlayButtonHeight / 2;
                mPlayButtonPaddingRate = (mPlayButtonMarginTopStart - mPlayButtonMarginTopTarget)
                        / scrollHeight;
            }

            public void handleScroll() {
                if (mIsLandscape) {
                    handleScrollLandscapeMode();
                    return;
                }
                int currentHeight = getHeaderHeight();
                float newMargin = 0;
                float lastHeight = 0;
                float newTextSize;
                // 1.FM description (margin)
                newMargin = getNewSize(currentHeight, mTargetHeight, mFmDescriptionMarginTopTarget,
                        mFmDescriptionPaddingRate);
                lastHeight = setNewPadding(mFmDescriptionText, newMargin);
                // 2. frequency text (text size and margin)
                newTextSize = getNewSize(currentHeight, mTargetHeight, mFrequencyTextSizeTarget,
                        mFrequencyTextSizeRate);
                mFrequencyText.setTextSize(newTextSize / mDensity);
                newMargin = getNewSize(currentHeight, mTargetHeight, mFrequencyMarginTopTarget,
                        mFrequencyPaddingRate);
                lastHeight = setNewPadding(mFrequencyText, newMargin + lastHeight);
                // 3. station name (margin and text size)
                newMargin = getNewSize(currentHeight, mTargetHeight, mStationNameMarginTopTarget,
                        mStationNamePaddingRate);
                lastHeight = setNewPadding(mStationNameText, newMargin + lastHeight);
                newTextSize = getNewSize(currentHeight, mTargetHeight, mStationNameTextSizeTarget,
                        mStationNameTextSizeRate);
                mStationNameText.setTextSize(newTextSize / mDensity);
                // 4. station rds (margin)
                newMargin = getNewSize(currentHeight, mTargetHeight, mStationRdsMarginTopTarget,
                        mStationRdsPaddingRate);
                lastHeight = setNewPadding(mStationRdsText, newMargin + lastHeight);
                // 5. control buttons (margin)
                newMargin = getNewSize(currentHeight, mTargetHeight, mControlViewMarginTopTarget,
                        mControlViewPaddingRate);
                setNewPadding(mControlView, newMargin + lastHeight);
                // 6. stop button (padding), it different to others, padding top refer to parent
                newMargin = getNewSize(currentHeight, mTargetHeight, mPlayButtonMarginTopTarget,
                        mPlayButtonPaddingRate);
                setNewPadding(mPlayButtonView, newMargin);
            }

            private void handleScrollLandscapeMode() {
                int currentHeight = getHeaderHeight();
                float newMargin = 0;
                float lastHeight = 0;
                float newTextSize;
                // 1. FM description (color, alpha and margin)
                newMargin = getNewSize(currentHeight, mTargetHeight, mFmDescriptionMarginTopTarget,
                        mFmDescriptionPaddingRate);
                setNewPadding(mFmDescriptionText, newMargin);

                newTextSize = getNewSize(currentHeight, mTargetHeight, mFmDescriptionTextSizeTarget,
                        mFmDescriptionTextSizeRate);
                mFmDescriptionText.setTextSize(newTextSize / mDensity);
                boolean reachTop = (mSecondTargetHeight == getHeaderHeight());
                mFmDescriptionText.setTextColor(getResources().getColor(R.color.header_fm_label_color));
                mFmDescriptionText.setAlpha(reachTop ? 0.87f : 1.0f);

                // 2. frequency text (text size, padding and margin)
                newTextSize = getNewSize(currentHeight, mTargetHeight, mFrequencyTextSizeTarget,
                        mFrequencyTextSizeRate);
                mFrequencyText.setTextSize(newTextSize / mDensity);
                newMargin = getNewSize(currentHeight, mTargetHeight, mFrequencyMarginTopTarget,
                        mFrequencyPaddingRate);
                // Move frequency text like "103.7" from middle to action bar in landscape,
                // or opposite direction. For example:
                // *************************          *************************
                // *                       *          * FM 103.7              *
                // * FM                    *   <-->   *                       *
                // * 103.7                 *          *                       *
                // *************************          *************************
                // "FM", "103.7" and other subviews are in a RelativeLayout (id actionbar_parent)
                // in main_header.xml. The position is controlled by the padding of each subview.
                // Because "FM" and "103.7" move up, we need to change the padding top and change
                // the padding left of "103.7".
                // The padding between "FM" and "103.7" is 0.2 (e.g. paddingRate) times
                // the length of "FM" string length.
                float paddingRate = 0.2f;
                float addPadding = (((1 + paddingRate) * computeFmDescriptionWidth())
                        * (mFullHeight - currentHeight)) / (mFullHeight - mTargetHeight);
                mFrequencyText.setPadding((int) (addPadding + mFmDescriptionStartPaddingLeft),
                        (int) (newMargin), mFrequencyText.getPaddingRight(),
                        mFrequencyText.getPaddingBottom());
                lastHeight = newMargin + lastHeight + mFrequencyText.getTextSize();
                // If frequency text move to action bar, change it to bold
                setNewTypefaceForFrequencyText();

                // 3. station name (text size and margin)
                newTextSize = getNewSize(currentHeight, mTargetHeight, mStationNameTextSizeTarget,
                        mStationNameTextSizeRate);
                mStationNameText.setTextSize(newTextSize / mDensity);
                newMargin = getNewSize(currentHeight, mTargetHeight, mStationNameMarginTopTarget,
                        mStationNamePaddingRate);
                // if move to target position, need not move over the edge of actionbar
                if (lastHeight <= mActionBarHeight) {
                    lastHeight = mActionBarHeight;
                }
                lastHeight = setNewPadding(mStationNameText, newMargin + lastHeight);
                /*
                 * 4. station rds (margin), in landscape with favorite
                 * it need parallel to station name
                 */
                newMargin = getNewSize(currentHeight, mTargetHeight, mStationRdsMarginTopTarget,
                        mStationRdsPaddingRate);
                int targetHeight = mFullHeight - (mFullHeight - mTargetHeight) / 2;
                if (currentHeight <= targetHeight) {
                    String stationName = "" + mStationNameText.getText();
                    int stationNameTextWidth = mStationNameText.getPaddingLeft();
                    if (!stationName.equals("")) {
                        Paint paint = mStationNameText.getPaint();
                        stationNameTextWidth += (int) paint.measureText(stationName) + 8;
                    }
                    mStationRdsText.setPadding((int) stationNameTextWidth,
                            (int) (newMargin + lastHeight), mStationRdsText.getPaddingRight(),
                            mStationRdsText.getPaddingBottom());
                } else {
                    mStationRdsText.setPadding((int) (16 * mDensity),
                            (int) (newMargin + lastHeight), mStationRdsText.getPaddingRight(),
                            mStationRdsText.getPaddingBottom());
                }
                // 5. control buttons (margin)
                newMargin = getNewSize(currentHeight, mTargetHeight, mControlViewMarginTopTarget,
                        mControlViewPaddingRate);
                setNewPadding(mControlView, newMargin + lastHeight);
                // 6. stop button (padding), it different to others, padding top refer to parent
                newMargin = getNewSize(currentHeight, mTargetHeight, mPlayButtonMarginTopTarget,
                        mPlayButtonPaddingRate);
                setNewPadding(mPlayButtonView, newMargin);
            }

            // Compute the text "FM" width
            private float computeFmDescriptionWidth() {
                Paint paint = mFmDescriptionText.getPaint();
                return (float) paint.measureText(mFmDescriptionText.getText().toString());
            }
        }

        private class SecondRangeAdjuster extends FirstRangeAdjuster {
            public SecondRangeAdjuster() {
                Resources res = mContext.getResources();
                mTargetHeight = mSecondTargetHeight;
                // init start
                mFrequencyStartTextSize = res
                        .getDimension(R.dimen.fm_frequency_text_size_first_target);
                mStationNameTextSizeStart = res
                        .getDimension(R.dimen.fm_station_name_text_size_first_target);
                mFmDescriptionMarginTopStart = res
                        .getDimension(R.dimen.fm_description_margin_top_first_target)
                        + mActionBarHeight;// first view, margin refer to parent
                mFrequencyMarginTopStart = res
                        .getDimension(R.dimen.fm_frequency_margin_top_first_target);
                mStationNameMarginTopStart = res
                        .getDimension(R.dimen.fm_station_name_margin_top_first_target);
                mStationRdsMarginTopStart = res
                        .getDimension(R.dimen.fm_station_rds_margin_top_first_target);
                mControlViewMarginTopStart = res
                        .getDimension(R.dimen.fm_control_buttons_margin_top_first_target);
                // init target
                mFrequencyTextSizeTarget = res
                        .getDimension(R.dimen.fm_frequency_text_size_second_target);
                mStationNameTextSizeTarget = res
                        .getDimension(R.dimen.fm_station_name_text_size_second_target);
                mFmDescriptionMarginTopTarget = res
                        .getDimension(R.dimen.fm_description_margin_top_second_target);
                mFrequencyMarginTopTarget = res
                        .getDimension(R.dimen.fm_frequency_margin_top_second_target);
                mStationNameMarginTopTarget = res
                        .getDimension(R.dimen.fm_station_name_margin_top_second_target);
                mStationRdsMarginTopTarget = res
                        .getDimension(R.dimen.fm_station_rds_margin_top_second_target);
                mControlViewMarginTopTarget = res
                        .getDimension(R.dimen.fm_control_buttons_margin_top_second_target);
                // init text size and margin adjust rate
                float scrollHeight = mFirstTargetHeight - mTargetHeight;
                mFrequencyTextSizeRate =
                        (mFrequencyStartTextSize - mFrequencyTextSizeTarget)
                        / scrollHeight;
                mStationNameTextSizeRate =
                        (mStationNameTextSizeStart - mStationNameTextSizeTarget)
                        / scrollHeight;
                mFmDescriptionPaddingRate =
                        (mFmDescriptionMarginTopStart - mFmDescriptionMarginTopTarget)

                        / scrollHeight;
                mFrequencyPaddingRate = (mFrequencyMarginTopStart - mFrequencyMarginTopTarget)
                        / scrollHeight;
                mStationNamePaddingRate = (mStationNameMarginTopStart - mStationNameMarginTopTarget)
                        / scrollHeight;
                mStationRdsPaddingRate = (mStationRdsMarginTopStart - mStationRdsMarginTopTarget)
                        / scrollHeight;
                mControlViewPaddingRate = (mControlViewMarginTopStart - mControlViewMarginTopTarget)
                        / scrollHeight;
                // init play button padding, it different to others, padding top refer to parent
                mPlayButtonHeight = res.getDimension(R.dimen.play_button_height);
                mPlayButtonMarginTopStart = mFullHeight - mPlayButtonHeight - 16 * mDensity;
                mPlayButtonMarginTopTarget = mFirstTargetHeight - mPlayButtonHeight / 2;
                mPlayButtonPaddingRate = (mPlayButtonMarginTopStart - mPlayButtonMarginTopTarget)
                        / scrollHeight;
            }

            @Override
            public void handleScroll() {
                int currentHeight = getHeaderHeight();
                float newMargin = 0;
                float lastHeight = 0;
                float newTextSize;
                // 1. FM description (alpha and margin)
                float alpha = 0f;
                int offset = (int) ((mFirstTargetHeight - currentHeight) / mDensity);// dip
                if (offset <= 0) {
                    alpha = 1f;
                } else if (offset <= 16) {
                    alpha = 1 - offset / 16f;
                }
                mFmDescriptionText.setAlpha(alpha);
                newMargin = getNewSize(currentHeight, mTargetHeight, mFmDescriptionMarginTopTarget,
                        mFmDescriptionPaddingRate);
                lastHeight = setNewPadding(mFmDescriptionText, newMargin);
                // 2. frequency text (text size and margin)
                newTextSize = getNewSize(currentHeight, mTargetHeight, mFrequencyTextSizeTarget,
                        mFrequencyTextSizeRate);
                mFrequencyText.setTextSize(newTextSize / mDensity);
                newMargin = getNewSize(currentHeight, mTargetHeight, mFrequencyMarginTopTarget,
                        mFrequencyPaddingRate);
                lastHeight = setNewPadding(mFrequencyText, newMargin + lastHeight);
                // If frequency text move to action bar, change it to bold
                setNewTypefaceForFrequencyText();
                // 3. station name (text size and margin)
                newTextSize = getNewSize(currentHeight, mTargetHeight, mStationNameTextSizeTarget,
                        mStationNameTextSizeRate);
                mStationNameText.setTextSize(newTextSize / mDensity);
                newMargin = getNewSize(currentHeight, mTargetHeight, mStationNameMarginTopTarget,
                        mStationNamePaddingRate);
                // if move to target position, need not move over the edge of actionbar
                if (lastHeight <= mActionBarHeight) {
                    lastHeight = mActionBarHeight;
                }
                lastHeight = setNewPadding(mStationNameText, newMargin + lastHeight);
                // 4. station rds (margin)
                newMargin = getNewSize(currentHeight, mTargetHeight, mStationRdsMarginTopTarget,
                        mStationRdsPaddingRate);
                lastHeight = setNewPadding(mStationRdsText, newMargin + lastHeight);
                // 5. control buttons (margin)
                newMargin = getNewSize(currentHeight, mTargetHeight, mControlViewMarginTopTarget,
                        mControlViewPaddingRate);
                setNewPadding(mControlView, newMargin + lastHeight);
                // 6. stop button (padding), it different to others, padding top refer to parent
                newMargin = currentHeight - mPlayButtonHeight / 2;
                setNewPadding(mPlayButtonView, newMargin);
            }
        }

        private void setNewTypefaceForFrequencyText() {
            boolean needBold = (mSecondTargetHeight == getHeaderHeight());
            mFrequencyText.setTypeface(needBold ? Typeface.SANS_SERIF : mDefaultFrequencyTypeface);
        }

        private float setNewPadding(TextView current, float newMargin) {
            current.setPadding(current.getPaddingLeft(), (int) (newMargin),
                    current.getPaddingRight(), current.getPaddingBottom());
            float nextLayoutPadding = newMargin + current.getTextSize();
            return nextLayoutPadding;
        }

        private void setNewPadding(View current, float newMargin) {
            float newPadding = newMargin;
            current.setPadding(current.getPaddingLeft(), (int) (newPadding),
                    current.getPaddingRight(), current.getPaddingBottom());
        }

        private float getNewSize(int currentHeight, int targetHeight,
                float targetSize, float rate) {
            if (currentHeight == targetHeight) {
                return targetSize;
            }
            return targetSize + (currentHeight - targetHeight) * rate;
        }
    }

    private final class ViewHolder {
        CardView mCardView;
        ImageView mMoreButton;
        FmVisualizerView mPlayIndicator;
        TextView mFmLabel;
        TextView mStationFreq;
        TextView mStationName;
        TextView mStationRt;
        View mPopupMenuAnchor;
    }
}
