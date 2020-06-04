/*
 * Copyright (C) 2019 The LineageOS Project
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

package org.lineageos.settings.popupcamera;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.UserManager;
import android.preference.PreferenceManager;

public class PopupCameraPreferences {

    private static final String TAG = "PopupCameraUtils";
    private static final boolean DEBUG = false;
    private Context mContext;
    private SharedPreferences mSharedPrefs;

    private static final String LED_EFFECT_KEY = "popup_led_effect";
    private static final boolean LED_EFFECT_DEFAULT_VALUE = true;

    private static final String SOUND_EFFECT_KEY = "popup_sound_effect";
    private static final String SOUND_EFFECT_DEFAULT_VALUE = "0";

    public PopupCameraPreferences(Context context) {
        mContext = context;
        ensureSharedPrefs();
    }

    private void ensureSharedPrefs(){
        UserManager um = mContext.getSystemService(UserManager.class);
        if (mContext.isCredentialProtectedStorage() && !um.isUserUnlocked()) {
            mSharedPrefs = null;
        }else{
            mSharedPrefs = PreferenceManager.getDefaultSharedPreferences(mContext);
        }
    }

    public String getSoundEffect() {
        ensureSharedPrefs();
        if (mSharedPrefs == null){
            return SOUND_EFFECT_DEFAULT_VALUE;
        }
        return mSharedPrefs.getString(SOUND_EFFECT_KEY, SOUND_EFFECT_DEFAULT_VALUE);
    }

    public boolean isLedAllowed() {
        ensureSharedPrefs();
        if (mSharedPrefs == null){
            return LED_EFFECT_DEFAULT_VALUE;
        }
        return mSharedPrefs.getBoolean(LED_EFFECT_KEY, LED_EFFECT_DEFAULT_VALUE);
    }
}
