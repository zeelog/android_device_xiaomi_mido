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

import android.app.ActionBar;
import android.app.Activity;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.Typeface;
import android.location.Location;
import android.location.LocationManager;
import android.media.AudioManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.Message;
import android.util.Log;
import android.util.TypedValue;
import android.view.LayoutInflater;
import android.view.Menu;
import android.view.MenuInflater;
import android.view.MenuItem;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.widget.AdapterView;
import android.widget.BaseAdapter;
import android.widget.GridView;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.RelativeLayout;
import android.widget.TextView;
import android.widget.Toast;

import com.android.fmradio.FmService.OnExitListener;
import com.android.fmradio.FmStation.Station;
import com.android.fmradio.Utils;

import android.support.v7.widget.CardView;

/**
 * This class interact with user, provider edit station information, such as add
 * to favorite, edit favorite, delete from favorite
 */
public class FmFavoriteActivity extends Activity {
    // Logging
    private static final String TAG = "FmFavoriteActivity";

    public static final String ACTIVITY_RESULT = "ACTIVITY_RESULT";

    private static final String SHOW_GPS_DIALOG = "SHOW_GPS_DIALOG";

    private static final String GPS_NOT_LOCATED_DIALOG = "GPS_NOT_LOCATED_DIALOG";

    LinearLayout mSearchTips = null;

    private Context mContext = null; // application context

    private OnExitListener mExitListener = null;

    private GridView mGridView;

    private MyFavoriteAdapter mMyAdapter;

    private ProgressBar mSearchProgress = null;

    private MenuItem mMenuRefresh = null;

    private LocationManager mLocationManager;

    private Location mCurLocation;

    private boolean mIsActivityForeground = true;

    /**
     * on create
     *
     * @param savedInstanceState The save instance state
     */
    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // Bind the activity to FM audio stream.
        setVolumeControlStream(AudioManager.STREAM_MUSIC);
        getWindow().requestFeature(Window.FEATURE_ACTION_BAR);
        setContentView(R.layout.station_list);
        mContext = getApplicationContext();

        final Cursor stationList = getData();

        // display action bar and navigation button
        ActionBar actionBar = getActionBar();
        actionBar.setTitle(getString(R.string.station_title) +
                (stationList.getCount() > 0 ?
                 " (" + stationList.getCount() + ")" : ""));
        actionBar.setDisplayHomeAsUpEnabled(true);
        mLocationManager = (LocationManager) getSystemService(Context.LOCATION_SERVICE);

        mMyAdapter = new MyFavoriteAdapter(mContext);
        mGridView = (GridView) findViewById(R.id.gridview);
        mSearchTips = (LinearLayout) findViewById(R.id.search_tips);
        mSearchProgress = (ProgressBar) findViewById(R.id.search_progress);

        mGridView.setAdapter(mMyAdapter); // set adapter
        mMyAdapter.swipResult(stationList);
        mGridView.setFocusable(false);
        mGridView.setFocusableInTouchMode(false);

        mGridView.setOnItemClickListener(new AdapterView.OnItemClickListener() {
            /**
             * Click card item will finish activity and pass value to other activity
             *
             * @param parent adapter view
             * @param view item view
             * @param position current position
             * @param id current id
             */
            @Override
            public void onItemClick(AdapterView<?> parent, View view, int position, long id) {
                int frequency = mMyAdapter.getStationFreq(position);
                if (frequency != -1) {
                    Intent intentResult = new Intent();
                    intentResult.putExtra(ACTIVITY_RESULT, frequency);
                    setResult(RESULT_OK, intentResult);
                    finish();
                }
            }
        });

        // Finish favorite when exit FM
        mExitListener = new FmService.OnExitListener() {
            @Override
            public void onExit() {
                Handler mainHandler = new Handler(Looper.getMainLooper());
                mainHandler.post(new Runnable() {
                    @Override
                    public void run() {
                        FmFavoriteActivity.this.finish();
                    }
                });
            }
        };
        FmService.registerExitListener(mExitListener);
        if (!mIsServiceBinded) {
            bindService();
        }
    }

    /**
     * When menu is selected
     *
     * @param item The selected menu item
     * @return true to consume it, false to can handle other
     */
    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        switch (item.getItemId()) {
            case android.R.id.home:
                onBackPressed();
                break;
            case R.id.fm_station_list_refresh:
                if (null != mService) {
                    refreshMenuItem(false);

                    mMyAdapter.swipResult(null);
                    mGridView.setEmptyView(mSearchTips);
                    mSearchProgress.setIndeterminate(true);

                    // If current location and last location exceed defined distance, delete the RDS database
                    if (isGpsOpen()) {
                        mCurLocation = mLocationManager
                                .getLastKnownLocation(LocationManager.GPS_PROVIDER);
                        if (mCurLocation != null) {
                            double[] lastLocations = FmUtils.getLastSearchedLocation(mContext);
                            float distance[] = new float[2];
                            Location.distanceBetween(lastLocations[0], lastLocations[1],
                                    mCurLocation.getLatitude(), mCurLocation.getLongitude(),
                                    distance);
                            float searchedDistance = distance[0];
                            boolean exceed =
                                    searchedDistance > FmUtils.LOCATION_DISTANCE_EXCEED;
                            mService.setDistanceExceed(exceed);
                            FmUtils.setLastSearchedLocation(mContext, mCurLocation.getLatitude(),
                                    mCurLocation.getLongitude());
                        }
                    }

                    mService.startScanAsync();
                }
                break;
            default:
                break;
        }
        return super.onOptionsItemSelected(item);
    }

    @Override
    public void onBackPressed() {
        if (null == mService) {
            Log.w(TAG, "onBackPressed, mService is null");
        } else {
            boolean isScanning = mService.isScanning();
            if (isScanning) {
                mService.stopScan();
            }
        }
        super.onBackPressed();
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        MenuInflater inflater = getMenuInflater();
        inflater.inflate(R.menu.fm_station_list_menu, menu);
        mMenuRefresh = menu.findItem(R.id.fm_station_list_refresh);
        return true;
    }

    @Override
    public boolean onPrepareOptionsMenu(Menu menu) {
        if (null != mService) {
            boolean isScan = mService.isScanning();
            refreshMenuItem(!isScan);
        }
        return super.onPrepareOptionsMenu(menu);
    }

    static final class ViewHolder {
        CardView mCardView;
        ImageView mStationTypeView;
        TextView mStationFreqView;
        TextView mStationNameView;
        TextView mStationRtView;
    }

    private Cursor getData() {
        Cursor cursor = mContext.getContentResolver().query(Station.CONTENT_URI,
                FmStation.COLUMNS, null, null, FmStation.Station.FREQUENCY);
        return cursor;
    }

    class MyFavoriteAdapter extends BaseAdapter {
        private Cursor mCursor;

        private LayoutInflater mInflater;
        private Context mContext;

        public MyFavoriteAdapter(Context context) {
            mInflater = LayoutInflater.from(context);
            mContext = context;
        }

        public void swipResult(Cursor cursor) {
            if (null != mCursor) {
                mCursor.close();
            }
            mCursor = cursor;
            notifyDataSetChanged();
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
                    params.addRule(RelativeLayout.RIGHT_OF, R.id.lv_fm_label);
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
                params.addRule(RelativeLayout.BELOW, R.id.lv_fm_label);
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
                convertView = mInflater.inflate(R.layout.station_gridview_item, null);

                int cardBgColor = mContext.getColor(R.color.favorite_tile_bg_color);
                cardBgColor = Utils.setColorAlphaComponent(cardBgColor, 60);
                viewHolder.mCardView = (CardView) convertView.findViewById(R.id.card_view);
                viewHolder.mCardView.setCardBackgroundColor(cardBgColor);

                viewHolder.mStationTypeView = (ImageView) convertView
                        .findViewById(R.id.lv_station_add_favorite);
                viewHolder.mStationFreqView = (TextView) convertView
                        .findViewById(R.id.lv_station_freq);
                viewHolder.mStationNameView = (TextView) convertView
                        .findViewById(R.id.lv_station_name);
                viewHolder.mStationRtView = (TextView) convertView
                        .findViewById(R.id.lv_station_rt);
                convertView.setTag(viewHolder);
            } else {
                viewHolder = (ViewHolder) convertView.getTag();
            }

            if (mCursor != null && mCursor.moveToFirst()) {
                mCursor.moveToPosition(position);
                final int stationFreq = mCursor.getInt(mCursor
                        .getColumnIndex(FmStation.Station.FREQUENCY));
                String name = mCursor.getString(mCursor
                        .getColumnIndex(FmStation.Station.STATION_NAME));
                String rt = mCursor.getString(mCursor
                        .getColumnIndex(FmStation.Station.RADIO_TEXT));
                final int isFavorite = mCursor.getInt(mCursor
                        .getColumnIndex(FmStation.Station.IS_FAVORITE));

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

                viewHolder.mStationFreqView.setText(FmUtils.formatStation(stationFreq));
                updateRDSViews(viewHolder.mStationFreqView,
                        viewHolder.mStationNameView, viewHolder.mStationRtView,
                        name, rt);
                viewHolder.mStationTypeView.setImageResource(0 == isFavorite ?
                        R.drawable.btn_fm_favorite_off_selector :
                        R.drawable.btn_fm_favorite_on_selector);
                int stationTypeViewBgColor = (0 == isFavorite) ?
                    R.color.addstation_off_button_color :
                    R.color.addstation_on_button_color;
                stationTypeViewBgColor = Utils.setColorAlphaComponent(
                            getResources().getColor(stationTypeViewBgColor), 10);
                viewHolder.mStationTypeView.getBackground().setTint(stationTypeViewBgColor);
                viewHolder.mStationTypeView.setOnClickListener(new View.OnClickListener() {
                    @Override
                    public void onClick(View v) {
                        if (0 == isFavorite) {
                            addFavorite(stationFreq);
                        } else {
                            deleteFavorite(stationFreq);
                        }
                    }
                });
            }
            return convertView;
        }

        private int getStationFreq(int position) {
            if (mCursor != null && mCursor.moveToPosition(position)) {
                final int stationFreq = mCursor.getInt(mCursor
                        .getColumnIndex(FmStation.Station.FREQUENCY));
                return stationFreq;
            }
            return -1;
        }
    }

    /**
     * Add searched station as favorite station
     */
    public void addFavorite(int stationFreq) {
        // TODO it's on UI thread, change to sub thread
        // update the station name and station type in database
        // according the frequency
        FmStation.addToFavorite(mContext, stationFreq);
        mMyAdapter.swipResult(getData());
    }

    /**
     * Delete favorite from favorite station list, make it as searched station
     */
    public void deleteFavorite(int stationFreq) {
        // TODO it's on UI thread, change to sub thread
        // update the station type from favorite to searched.
        FmStation.removeFromFavorite(mContext, stationFreq);
        mMyAdapter.swipResult(getData());
    }

    @Override
    protected void onResume() {
        super.onResume();
        mIsActivityForeground = true;
        if (null != mService) {
            mService.setFmMainActivityForeground(mIsActivityForeground);
            if (FmRecorder.STATE_RECORDING != mService.getRecorderState()) {
                mService.removeNotification();
            }
        }
    }

    @Override
    protected void onPause() {
        mIsActivityForeground = false;
        if (null != mService) {
            mService.setFmMainActivityForeground(mIsActivityForeground);
        }
        super.onPause();
    }

    @Override
    protected void onStop() {
       // FmUtils.updateFrontActivity(mContext, "");
        if (null != mService) {
            // home key pressed, show notification
            mService.setNotificationClsName(FmFavoriteActivity.class.getName());
            mService.updatePlayingNotification();
        }
        super.onStop();
    }

    @Override
    protected void onDestroy() {
        mMyAdapter.swipResult(null);
        FmService.unregisterExitListener(mExitListener);
        if (mService != null) {
            mService.unregisterFmRadioListener(mFmRadioListener);
        }
        unbindService();
        super.onDestroy();
    }

    private FmService mService = null;

    private boolean mIsServiceBinded = false;

    private void bindService() {
        mIsServiceBinded = bindService(new Intent(FmFavoriteActivity.this, FmService.class),
                mServiceConnection, Context.BIND_AUTO_CREATE);
        if (!mIsServiceBinded) {
            Log.e(TAG, "bindService, mIsServiceBinded is false");
            finish();
        }
    }

    private void unbindService() {
        if (mIsServiceBinded) {
            unbindService(mServiceConnection);
        }
    }

    // Service listener
    private FmListener mFmRadioListener = new FmListener() {
        @Override
        public void onCallBack(Bundle bundle) {
            int flag = bundle.getInt(FmListener.CALLBACK_FLAG);
            if (flag == FmListener.MSGID_FM_EXIT) {
                mHandler.removeCallbacksAndMessages(null);
            }

            // remove tag message first, avoid too many same messages in queue.
            Message msg = mHandler.obtainMessage(flag);
            msg.setData(bundle);
            mHandler.removeMessages(flag);
            mHandler.sendMessage(msg);
        }
    };

    /**
     * Main thread handler to update UI
     */
    private Handler mHandler = new Handler() {
        @Override
        public void handleMessage(Message msg) {
            Log.d(TAG,
                    "handleMessage, what = " + msg.what + ",hashcode:"
                            + mHandler.hashCode());
            Bundle bundle;
            switch (msg.what) {
                case FmListener.MSGID_SCAN_FINISHED:
                    bundle = msg.getData();
                    // cancel scan happen
                    boolean isScan = bundle.getBoolean(FmListener.KEY_IS_SCAN);
                    int searchedNum = bundle.getInt(FmListener.KEY_STATION_NUM);
                    refreshMenuItem(true);
                    mMyAdapter.swipResult(getData());
                    mService.updatePlayingNotification();
                    if (searchedNum == 0) {
                        Toast.makeText(mContext, getString(R.string.toast_cannot_search),
                                Toast.LENGTH_SHORT).show();
                        // searched station is zero, if db has station, should not use empty.
                        if (mMyAdapter.getCount() == 0) {
                            View emptyView = (View) findViewById(R.id.empty_tips);
                            emptyView.setVisibility(View.VISIBLE);
                            View searchTips = (View) findViewById(R.id.search_tips);
                            searchTips.setVisibility(View.GONE);
                        }
                        return;
                    }
                    // Show toast to tell user how many stations have been searched
                    String text = getString(R.string.toast_station_searched) + " "
                            + String.valueOf(searchedNum);
                    Toast.makeText(mContext, text, Toast.LENGTH_SHORT).show();
                    break;
                case FmListener.MSGID_SWITCH_ANTENNA:
                    bundle = msg.getData();
                    boolean isHeadset = bundle.getBoolean(FmListener.KEY_IS_SWITCH_ANTENNA);
                    // nothing to do to UI since we're supporting wireless mode
                default:
                    break;
            }
        }
    };

    private void refreshMenuItem(boolean enabled) {
        // action menu
        if (mMenuRefresh != null) {
            mMenuRefresh.setEnabled(enabled);
        }
    }

    // When call bind service, it will call service connect. register call back
    // listener and initial device
    private final ServiceConnection mServiceConnection = new ServiceConnection() {

        /**
         * called by system when bind service
         *
         * @param className component name
         * @param service service binder
         */
        @Override
        public void onServiceConnected(ComponentName className, IBinder service) {
            mService = ((FmService.ServiceBinder) service).getService();
            if (null == mService) {
                Log.e(TAG, "onServiceConnected, mService is null");
                finish();
                return;
            }
            mService.registerFmRadioListener(mFmRadioListener);
            mService.setFmMainActivityForeground(mIsActivityForeground);
            if (FmRecorder.STATE_RECORDING != mService.getRecorderState()) {
                mService.removeNotification();
            }
            // FmUtils.isFirstEnterStationList() must be called at the first time.
            // After it is called, it will save status to SharedPreferences.
            if (FmUtils.isFirstEnterStationList(mContext) || (0 == mMyAdapter.getCount())) {
                refreshMenuItem(false);
                mGridView.setEmptyView(mSearchTips);
                mSearchProgress.setIndeterminate(true);
                mMyAdapter.swipResult(null);
                mService.startScanAsync();
            } else {
                boolean isScan = mService.isScanning();
                if (isScan) {
                    mMyAdapter.swipResult(null);
                    mGridView.setEmptyView(mSearchTips);
                    mSearchProgress.setIndeterminate(true);
                } else {
                    // TODO it's on UI thread, change to sub thread
                    mMyAdapter.swipResult(getData());
                }
                refreshMenuItem(!isScan);
            }
        }

        /**
         * When unbind service will call this method
         *
         * @param className The component name
         */
        @Override
        public void onServiceDisconnected(ComponentName className) {
        }
    };

    /**
     * check gps is open or not
     *
     * @return true is open
     */
    private boolean isGpsOpen() {
        return mLocationManager.isProviderEnabled(android.location.LocationManager.GPS_PROVIDER);
    }
}
