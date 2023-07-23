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

package com.android.fmradio;

import android.content.ContentValues;
import android.content.Context;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.net.Uri;
import android.preference.PreferenceManager;
import android.provider.BaseColumns;
import android.text.TextUtils;

/**
 * This class provider interface to operator databases, use by activity and
 * service
 */
public class FmStation {
    private static final String TAG = "FmStation";
    // authority use composite content provider uri
    public static final String AUTHORITY = "com.android.fmradio";
    // use to composite content provider uri
    public static final String STATION = "station";
    // store current station in share preference with this key
    public static final String CURRENT_STATION = "curent_station";

    public static final String[] COLUMNS = new String[] {
        Station._ID,
        Station.FREQUENCY,
        Station.IS_FAVORITE,
        Station.STATION_NAME,
        Station.PROGRAM_SERVICE,
        Station.RADIO_TEXT,
    };

    /**
     * This class provider the columns of StationList table
     */
    public static final class Station implements BaseColumns {
        public static final Uri CONTENT_URI = Uri.parse("content://" + AUTHORITY + "/" + STATION);

        /**
         * Station frequency(Hz)
         * <P>Type: INTEGER </P>
         */
        public static final String FREQUENCY = "frequency";

        /**
         * If this station is favorite, it is 1, otherwise 0
         * <P>Type: INTEGER (boolean)</P>
         */
        public static final String IS_FAVORITE = "is_favorite";

        /**
         * Station name, if user rename a station, this must be not null, otherwise is null.
         * <P>Type: TEXT</P>
         */
        public static final String STATION_NAME = "station_name";

        /**
         * Program service(PS), station name provide by RDS station
         * <P>Type: TEXT</P>
         */
        public static final String PROGRAM_SERVICE = "program_service";

        /**
         * Radio text(RT or rds), detail ration text provide by RDS station.
         * <P>Type: TEXT</P>
         */
        public static final String RADIO_TEXT = "radio_text";
    }

    /**
     * Insert station information to database
     *
     * @param context The context
     * @param frequency The station frequency
     * @param stationName The station name
     */
    public static void insertStationToDb(Context context, int frequency, String stationName) {
        ContentValues values = new ContentValues(2);
        values.put(Station.FREQUENCY, frequency);
        values.put(Station.STATION_NAME, stationName);
        context.getContentResolver().insert(Station.CONTENT_URI, values);
    }

    /**
     * Insert station information to database with given frequency, station name, PS and RT
     *
     * @param context The context
     * @param frequency The station frequency
     * @param stationName The station name
     * @param ps The program service
     * @param rt The radio text
     */
    public static void insertStationToDb(
            Context context, int frequency, String stationName, String ps, String rt) {
        ContentValues values = new ContentValues(4);
        values.put(Station.FREQUENCY, frequency);
        values.put(Station.STATION_NAME, stationName);
        values.put(Station.PROGRAM_SERVICE, ps);
        values.put(Station.RADIO_TEXT, rt);
        context.getContentResolver().insert(Station.CONTENT_URI, values);
    }

    /**
     * Insert station information to database with given values
     *
     * @param context The context
     * @param values Need inserted values
     */
    public static void insertStationToDb(Context context, ContentValues values) {
        context.getContentResolver().insert(Station.CONTENT_URI, values);
    }

    /**
     * Update station name according to given frequency
     *
     * @param context The context
     * @param frequency the station frequency need to update
     * @param stationName The new station's name
     */
    public static void updateStationToDb(Context context, int frequency, String stationName) {
        final int size = 1;
        ContentValues values = new ContentValues(size);
        values.put(Station.STATION_NAME, stationName);
        context.getContentResolver().update(
                Station.CONTENT_URI,
                values,
                Station.FREQUENCY + "=?",
                new String[] { String.valueOf(frequency)});
    }

    /**
     * Update station information according to given frequency
     *
     * @param context The context
     * @param frequency the station frequency need to update
     * @param values The new station's values
     */
    public static void updateStationToDb(Context context, int frequency, ContentValues values) {
        context.getContentResolver().update(
                Station.CONTENT_URI,
                values,
                Station.FREQUENCY + "=?",
                new String[] { String.valueOf(frequency)});
    }

    /**
     * Delete station according to given frequency
     *
     * @param context The context
     * @param frequency The station frequency
     */
    public static void deleteStationInDb(Context context, int frequency) {
        context.getContentResolver().delete(
                Station.CONTENT_URI,
                Station.FREQUENCY + "=?",
                new String[] { String.valueOf(frequency)});
    }

    /**
     * Check whether the given frequency station is exist in database
     *
     * @param context The context
     * @param frequency The station frequency
     *
     * @return true or false indicate whether station is exist
     */
    public static boolean isStationExist(Context context, int frequency) {
        boolean isExist = false;
        Cursor cursor = null;
        try {
            cursor = context.getContentResolver().query(
                Station.CONTENT_URI,
                new String[] { Station.STATION_NAME },
                Station.FREQUENCY + "=?",
                new String[] { String.valueOf(frequency) },
                null);
            if (cursor != null && cursor.moveToFirst()) {
                isExist = true;
            }
        } finally {
            if (cursor != null) {
                cursor.close();
            }
        }
        return isExist;
    }

    /**
     * Get current station from share preference
     *
     * @param context The context
     *
     * @return the current station in store share preference
     */
    public static int getCurrentStation(Context context) {
        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(context);
        int currentStation = prefs.getInt(CURRENT_STATION, FmUtils.DEFAULT_STATION);
        return currentStation;
    }

    /**
     * store current station to share preference
     *
     * @param context The context
     * @param frequency The current station frequency
     */
    public static void setCurrentStation(Context context, int frequency) {
        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(context);
        SharedPreferences.Editor editor = prefs.edit();
        editor.putInt(CURRENT_STATION, frequency);
        editor.commit();
    }

    /**
     * Get station name, if user rename station, we return user set station name, otherwise
     * return program service.
     *
     * @param context The context
     * @param frequency The station frequency
     *
     * @return The station name
     */
    public static String getStationName(Context context, int frequency) {
        String stationName = null;
        Cursor cursor = null;
        try {
            cursor = context.getContentResolver().query(
                    Station.CONTENT_URI,
                    new String[] { Station.STATION_NAME, Station.PROGRAM_SERVICE },
                    Station.FREQUENCY + "=?",
                    new String[] { String.valueOf(frequency) },
                    null);
            if (cursor != null && cursor.moveToFirst()) {
                // If the station name is not exist, show program service(PS) instead
                stationName = cursor.getString(cursor.getColumnIndex(Station.STATION_NAME));
                if (TextUtils.isEmpty(stationName)) {
                    stationName = cursor.getString(cursor.getColumnIndex(Station.PROGRAM_SERVICE));
                }
            }
        } finally {
            if (cursor != null) {
                cursor.close();
            }
        }
        return stationName;
    }

    /**
     * Judge whether station is a favorite station
     *
     * @param context The context
     * @param frequency The station frequency
     *
     * @return true or false indicate whether the station is favorite
     */
    public static boolean isFavoriteStation(Context context, int frequency) {
        boolean isFavorite = false;
        Cursor cursor = null;
        try {
            cursor = context.getContentResolver().query(
                Station.CONTENT_URI,
                new String[] { Station.IS_FAVORITE },
                Station.FREQUENCY + "=?",
                new String[] { String.valueOf(frequency) },
                null);
            if (cursor != null && cursor.moveToFirst()) {
                isFavorite = cursor.getInt(0) > 0;
            }
        } finally {
            if (cursor != null) {
                cursor.close();
            }
        }
        return isFavorite;
    }

    /**
     * update db to mark it is a favorite frequency
     *
     * @param context The context
     * @param frequency The target frequency
     */
    public static void addToFavorite(Context context, int frequency) {
        ContentValues values = new ContentValues(1);
        values.put(Station.IS_FAVORITE, true);
        context.getContentResolver().update(
                Station.CONTENT_URI,
                values,
                Station.FREQUENCY + "=?",
                new String[] { String.valueOf(frequency) });
    }

    /**
     * update db to mark it is a normal frequency
     *
     * @param context The context
     * @param frequency The target frequency
     */
    public static void removeFromFavorite(Context context, int frequency) {
        ContentValues values = new ContentValues(1);
        values.put(Station.IS_FAVORITE, false);
        values.put(Station.STATION_NAME, "");
        context.getContentResolver().update(
                Station.CONTENT_URI,
                values,
                Station.FREQUENCY + "=?",
                new String[] { String.valueOf(frequency) });
    }

    /**
     * Get station count
     *
     * @param context The context
     *
     * @return The numbers of station
     */
    public static int getStationCount(Context context) {
        int stationNus = 0;
        Cursor cursor = null;
        try {
            cursor = context.getContentResolver().query(
                    Station.CONTENT_URI,
                    new String[] { Station._ID },
                    null,
                    null,
                    null);
                if (cursor != null) {
                    stationNus = cursor.getCount();
                }
        } finally {
            if (cursor != null) {
                cursor.close();
            }
        }
        return stationNus;
    }

    /**
     * Clean all stations which station type is searched
     *
     * @param context The context
     */
    public static void cleanSearchedStations(Context context) {
        context.getContentResolver().delete(Station.CONTENT_URI,
                Station.IS_FAVORITE + "=0", null);
    }

    /**
     * Clear all station of FMRadio database
     *
     * @param context The context
     */
    public static void cleanAllStations(Context context) {
        context.getContentResolver().delete(Station.CONTENT_URI, null, null);
    }
}
