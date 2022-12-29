/*
 * Copyright (C) 2018 The LineageOS Project
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

package org.lineageos.settings.dirac;

import android.content.res.Resources;
import android.service.quicksettings.Tile;
import android.service.quicksettings.TileService;

import java.util.HashMap;
import java.util.Map;

import org.lineageos.settings.R;

public class DiracTileService extends TileService {
    private DiracUtils mDiracUtils;
    private Map<String, String> mPresetMap = new HashMap<>();
    private String mPresetDefault;

    @Override
    public void onStartListening() {
        mDiracUtils = new DiracUtils(getApplicationContext());

        final boolean enhancerEnabled = mDiracUtils.isDiracEnabled();

        final Tile tile = getQsTile();
        if (enhancerEnabled) {
            tile.setState(Tile.STATE_ACTIVE);
        } else {
            tile.setState(Tile.STATE_INACTIVE);
        }

        if (mPresetMap.isEmpty()) {
            final Resources res = getApplicationContext().getResources();
            final String[] entries =
                res.getStringArray(R.array.dirac_preset_pref_entries);
            final String[] values =
                res.getStringArray(R.array.dirac_preset_pref_values);

            mPresetDefault = entries[0];
            for (int i = 0; i < values.length; i++) {
                mPresetMap.put(values[i], entries[i]);
            }
        }

        final String level = mDiracUtils.getLevel();
        tile.setSubtitle(mPresetMap.getOrDefault(level, mPresetDefault));
        tile.updateTile();

        super.onStartListening();
    }

    @Override
    public void onClick() {
        final Tile tile = getQsTile();
        if (mDiracUtils.isDiracEnabled()) {
            mDiracUtils.setEnabled(false);
            tile.setState(Tile.STATE_INACTIVE);
        } else {
            mDiracUtils.setEnabled(true);
            tile.setState(Tile.STATE_ACTIVE);
        }
        tile.updateTile();
        super.onClick();
    }
}
