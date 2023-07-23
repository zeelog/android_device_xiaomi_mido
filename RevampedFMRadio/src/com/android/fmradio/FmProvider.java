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

import android.content.ContentProvider;
import android.content.ContentUris;
import android.content.ContentValues;
import android.content.Context;
import android.content.UriMatcher;
import android.database.Cursor;
import android.database.sqlite.SQLiteDatabase;
import android.database.sqlite.SQLiteOpenHelper;
import android.database.sqlite.SQLiteQueryBuilder;
import android.net.Uri;
import android.text.TextUtils;
import android.util.Log;

/**
 * This class provider interface to operator FM database table StationList
 */
public class FmProvider extends ContentProvider {
    private static final String TAG = "FmProvider";

    // database instance use to operate the database
    private SQLiteDatabase mSqlDb = null;
    // database helper use to get database instance
    private DatabaseHelper mDbHelper = null;
    // database name
    private static final String DATABASE_NAME = "FmRadio.db";
    // database version
    private static final int DATABASE_VERSION = 1;
    // table name
    private static final String TABLE_NAME = "StationList";

    // URI match code
    private static final int STATION_FREQ = 1;
    // URI match code
    private static final int STATION_FREQ_ID = 2;
    // use to match URI
    private static final UriMatcher URI_MATCHER = new UriMatcher(UriMatcher.NO_MATCH);

    // match URI with station frequency or station frequency id
    static {
        URI_MATCHER.addURI(FmStation.AUTHORITY, FmStation.STATION, STATION_FREQ);
        URI_MATCHER.addURI(FmStation.AUTHORITY, FmStation.STATION + "/#",
                STATION_FREQ_ID);
    }

    /**
     * Helper to operate database
     */
    private static class DatabaseHelper extends SQLiteOpenHelper {

        /**
         * initial database name and database version
         *
         * @param context application context
         */
        DatabaseHelper(Context context) {
            super(context, DATABASE_NAME, null, DATABASE_VERSION);
        }

        /**
         * Create database table
         *
         * @param db The database
         */
        @Override
        public void onCreate(SQLiteDatabase db) {
            // Create the table
            Log.d(TAG, "onCreate, create the database");
            db.execSQL(
                    "CREATE TABLE " + TABLE_NAME + "("
                            + FmStation.Station._ID + " INTEGER PRIMARY KEY AUTOINCREMENT,"
                            + FmStation.Station.FREQUENCY + " INTEGER UNIQUE,"
                            + FmStation.Station.IS_FAVORITE + " INTEGER DEFAULT 0,"
                            + FmStation.Station.STATION_NAME + " TEXT,"
                            + FmStation.Station.PROGRAM_SERVICE + " TEXT,"
                            + FmStation.Station.RADIO_TEXT + " TEXT"
                            + ");"
                    );
        }

        /**
         * Upgrade database
         *
         * @param db database
         * @param oldVersion The old database version
         * @param newVersion The new database version
         */
        @Override
        public void onUpgrade(SQLiteDatabase db, int oldVersion, int newVersion) {
            // TODO: reimplement this when dB version changes
            Log.i(TAG, "onUpgrade, upgrading database from version " + oldVersion + " to "
                    + newVersion + ", which will destroy all old data");
            db.execSQL("DROP TABLE IF EXISTS " + TABLE_NAME);
            onCreate(db);
        }
    }

    /**
     * Delete database table rows with condition
     *
     * @param uri The uri to delete
     * @param selection The where cause to apply, if null will delete all rows
     * @param selectionArgs The select value
     *
     * @return The rows number has be deleted
     */
    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        int rows = 0;
        mSqlDb = mDbHelper.getWritableDatabase();
        switch (URI_MATCHER.match(uri)) {
            case STATION_FREQ:
                rows = mSqlDb.delete(TABLE_NAME, selection, selectionArgs);
                getContext().getContentResolver().notifyChange(uri, null);
                break;

            case STATION_FREQ_ID:
                String stationID = uri.getPathSegments().get(1);
                rows = mSqlDb.delete(TABLE_NAME,
                        FmStation.Station._ID
                                + "="
                                + stationID
                                + (TextUtils.isEmpty(selection) ? "" : " AND (" + selection + ")"),
                        selectionArgs);
                getContext().getContentResolver().notifyChange(uri, null);
                break;

            default:
                Log.e(TAG, "delete, unkown URI to delete: " + uri);
                break;
        }
        return rows;
    }

    /**
     * Insert values to database with uri
     *
     * @param uri The insert uri
     * @param values The insert values
     *
     * @return The uri after inserted
     */
    @Override
    public Uri insert(Uri uri, ContentValues values) {
        Uri rowUri = null;
        mSqlDb = mDbHelper.getWritableDatabase();
        ContentValues v = new ContentValues(values);

        long rowId = mSqlDb.insert(TABLE_NAME, null, v);
        if (rowId <= 0) {
            Log.e(TAG, "insert, failed to insert row into " + uri);
        }
        rowUri = ContentUris.appendId(FmStation.Station.CONTENT_URI.buildUpon(), rowId)
                .build();
        getContext().getContentResolver().notifyChange(rowUri, null);
        return rowUri;
    }

    /**
     * Create database helper
     *
     * @return true if create database helper success
     */
    @Override
    public boolean onCreate() {
        mDbHelper = new DatabaseHelper(getContext());
        return (null == mDbHelper) ? false : true;
    }

    /**
     * Query the database with current settings and add information
     *
     * @param uri The database uri
     * @param projection The columns need to query
     * @param selection The where clause
     * @param selectionArgs The where value
     * @param sortOrder The column to sort
     *
     * @return The query result cursor
     */
    @Override
    public Cursor query(Uri uri, String[] projection, String selection, String[] selectionArgs,
            String sortOrder) {
        SQLiteQueryBuilder qb = new SQLiteQueryBuilder();
        SQLiteDatabase db = mDbHelper.getReadableDatabase();
        qb.setTables(TABLE_NAME);

        int match = URI_MATCHER.match(uri);

        if (STATION_FREQ_ID == match) {
            qb.appendWhere("_id = " + uri.getPathSegments().get(1));
        }

        Cursor c = qb.query(db, projection, selection, selectionArgs, null, null, sortOrder);
        if (null != c) {
            c.setNotificationUri(getContext().getContentResolver(), uri);
        }
        return c;
    }

    /**
     * Update the database content use content values with current settings and
     * add information
     *
     * @param uri The database uri
     * @param values The values need to update
     * @param selection The where clause
     * @param selectionArgs The where value
     *
     * @return The row numbers have changed
     */
    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) {
        int rows = 0;
        mSqlDb = mDbHelper.getWritableDatabase();
        switch (URI_MATCHER.match(uri)) {
            case STATION_FREQ:
                rows = mSqlDb.update(TABLE_NAME, values, selection, selectionArgs);
                getContext().getContentResolver().notifyChange(uri, null);
                break;
            case STATION_FREQ_ID:
                String stationID = uri.getPathSegments().get(1);
                rows = mSqlDb.update(TABLE_NAME,
                        values,
                        FmStation.Station._ID
                                + "="
                                + stationID
                                + (TextUtils.isEmpty(selection) ? "" : " AND (" + selection + ")"),
                        selectionArgs);
                getContext().getContentResolver().notifyChange(uri, null);
                break;
            default:
                Log.e(TAG, "update, unkown URI to update: " + uri);
                break;
        }
        return rows;
    }

    /**
     * Get uri type
     *
     * @param uri The the uri
     *
     * @return The type
     */
    @Override
    public String getType(Uri uri) {
        return null;
    }
}
