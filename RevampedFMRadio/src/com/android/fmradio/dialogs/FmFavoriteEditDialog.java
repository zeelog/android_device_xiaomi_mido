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

package com.android.fmradio.dialogs;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.Dialog;
import android.app.DialogFragment;
import android.content.DialogInterface;
import android.os.Bundle;
import android.text.Editable;
import android.text.InputFilter;
import android.text.Selection;
import android.text.TextUtils;
import android.text.TextWatcher;
import android.view.View;
import android.view.View.OnClickListener;
import android.widget.Button;
import android.widget.EditText;
import android.widget.TextView;

import com.android.fmradio.R;

/**
 * Edit favorite station name and frequency, caller should implement
 * EditFavoriteListener
 */
public class FmFavoriteEditDialog extends DialogFragment {
    private static final String STATION_NAME = "station_name";
    private static final String STATION_FREQ = "station_freq";
    private EditFavoriteListener mListener = null;
    private TextView mTitle = null;
    private EditText mStationNameEditor = null;
    private Button mButtonSave = null;
    private Button mButtonDiscard = null;

    /**
     * Create edit favorite dialog instance, caller should implement edit
     * favorite listener
     *
     * @param stationName The station name
     * @param stationFreq The station frequency
     * @return edit favorite dialog
     */
    public static FmFavoriteEditDialog newInstance(String stationName, int stationFreq) {
        FmFavoriteEditDialog fragment = new FmFavoriteEditDialog();
        Bundle args = new Bundle(2);
        args.putString(STATION_NAME, stationName);
        args.putInt(STATION_FREQ, stationFreq);
        fragment.setArguments(args);
        return fragment;
    }

    /**
     * Edit favorite listener
     */
    public interface EditFavoriteListener {
        /**
         * Edit favorite station name and station frequency
         */
        void editFavorite(int stationFreq, String name);
    }

    @Override
    public void onAttach(Activity activity) {
        super.onAttach(activity);
        try {
            mListener = (EditFavoriteListener) activity;
        } catch (ClassCastException e) {
            e.printStackTrace();
        }
    }

    @Override
    public Dialog onCreateDialog(Bundle savedInstanceState) {
        String stationName = getArguments().getString(STATION_NAME);
        View view = View.inflate(getActivity(), R.layout.alertdialog, null);
        mStationNameEditor = (EditText) view.findViewById(
                R.id.alertdialog_edittext);

        if (null == stationName || "".equals(stationName.trim())) {
            stationName = "";
        }

        mTitle = (TextView) view.findViewById(R.id.alertdialog_title);
        mTitle.setText(R.string.rename);

        mStationNameEditor.requestFocus();
        mStationNameEditor.requestFocusFromTouch();
        // Edit
        mStationNameEditor.setText(stationName);
        mStationNameEditor.setHint(R.string.station_rename_hint);
        mStationNameEditor.setFilters(new InputFilter[] {
            new InputFilter.LengthFilter(60)
        });
        Editable text = mStationNameEditor.getText();
        Selection.setSelection(text, text.length());

        mButtonSave = (Button) view.findViewById(R.id.alertdialog_button_ok);
        mButtonSave.setText(R.string.save);
        mButtonSave.setOnClickListener(mButtonOnClickListener);

        mButtonDiscard = (Button) view.findViewById(R.id.alertdialog_button_cancel);
        mButtonDiscard.setText(android.R.string.cancel);
        mButtonDiscard.setOnClickListener(mButtonOnClickListener);

        return new AlertDialog.Builder(getActivity()).setView(view).create();
    }

    /**
     * Set the dialog edit text and other attribute.
     */
    @Override
    public void onResume() {
        super.onResume();
        setTextChangedCallback();
        String toName = mStationNameEditor.getText().toString();
        // empty or blank or white space only name is not allowed
        toggleSaveButton(toName != null && TextUtils.getTrimmedLength(toName) > 0);
    }

    /**
     * This method register callback and set filter to Edit, in order to make
     * sure that user input is legal. The input can't be empty/blank/all-spaces filename
     */
    private void setTextChangedCallback() {
        mStationNameEditor.addTextChangedListener(new TextWatcher() {
            // not use, so don't need to implement it
            @Override
            public void afterTextChanged(Editable arg0) {
            }

            // not use, so don't need to implement it
            @Override
            public void beforeTextChanged(CharSequence s, int start, int count, int after) {
            }

            /**
             * check user input whether is null or all white space.
             */
            @Override
            public void onTextChanged(CharSequence s, int start, int before, int count) {
                // empty or blank or white space only name is not allowed
                toggleSaveButton(TextUtils.getTrimmedLength(s) > 0);
            }
        });
    }

    /**
     * This method enables or disables save button to forbid renaming station name to null.
     * @param isEnabled true to enable save button, false to disable save button
     */
    private void toggleSaveButton(boolean isEnabled) {
        final AlertDialog dialog = (AlertDialog) getDialog();
        if (dialog == null) {
            return;
        }
        mButtonSave.setEnabled(isEnabled);
    }

    private OnClickListener mButtonOnClickListener = new OnClickListener() {
        /**
         * Define the button operation
         */
        @Override
        public void onClick(View v) {

            switch (v.getId()) {
                case R.id.alertdialog_button_ok:
                    String newName = mStationNameEditor.getText().toString().trim();
                    final int stationFreq = getArguments().getInt(STATION_FREQ);
                    mListener.editFavorite(stationFreq, newName);
                    dismissAllowingStateLoss();
                break;

                case R.id.alertdialog_button_cancel:
                    dismissAllowingStateLoss();
                    break;

                default:
                    break;
            }
        }
    };
}
