package org.lineageos.settings.kcal;

import android.app.AlertDialog;
import android.app.Dialog;
import android.app.DialogFragment;
import android.app.FragmentManager;
import android.content.DialogInterface;
import android.os.Bundle;

import org.lineageos.settings.R;

public class PresetDialog extends DialogFragment {
    private CharSequence[] mEntries;
    private CharSequence[] mEntryValues;
    private String mValue;
    private KCalSettings mKCalSettingsFragment;
    private int mClickedDialogEntryIndex;
    private final DialogInterface.OnClickListener selectItemListener =
            new DialogInterface.OnClickListener() {

                @Override
                public void onClick(DialogInterface dialog, int which) {
                    if (mClickedDialogEntryIndex != which) {
                        mValue = mEntryValues[which].toString();
                        mKCalSettingsFragment.applyValues(mValue);
                        mClickedDialogEntryIndex = which;
                    }
                    dialog.dismiss();
                }
            };

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        mEntries = getResources().getStringArray(R.array.preset_entries);
        mEntryValues = getResources().getStringArray(R.array.preset_values);
    }

    @Override
    public Dialog onCreateDialog(Bundle savedInstanceState) {
        AlertDialog.Builder dialog = new AlertDialog.Builder(getActivity());
        dialog.setTitle(getString(R.string.presets_dialog_title));
        mClickedDialogEntryIndex = getValueIndex();
        dialog.setItems(mEntries, selectItemListener);
        return dialog.create();
    }

    private int getValueIndex() {
        return findIndexOfValue(mValue);
    }

    private int findIndexOfValue(String value) {
        if (value != null && mEntryValues != null) {
            for (int i = mEntryValues.length - 1; i >= 0; i--) {
                if (mEntryValues[i].equals(value)) {
                    return i;
                }
            }
        }
        return -1;
    }

    public void show(FragmentManager manager, String tag, KCalSettings mKCalSettingsFragment) {
        super.show(manager, tag);
        this.mKCalSettingsFragment = mKCalSettingsFragment;
    }
}
