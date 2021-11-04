/*
 * Copyright (C) 2020 The LineageOS Project
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

package org.lineageos.settings.refreshrate;

import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.UserHandle;
import android.view.Display;

import android.provider.Settings;
import androidx.preference.PreferenceManager;

public final class RefreshUtils {

    private static final String REFRESH_CONTROL = "refresh_control";
    private static final String REFRESH_SERVICE = "refresh_service";

    private static final String KEY_PEAK_REFRESH_RATE = "peak_refresh_rate";
    private static final String KEY_MIN_REFRESH_RATE = "min_refresh_rate";
    private Context mContext;

    protected static final int STATE_DEFAULT = 0;
    protected static final int STATE_MEDIUM = 1;
    protected static final int STATE_HIGH = 2;

    private static final float REFRESH_STATE_DEFAULT = 60f;
    private static final float REFRESH_STATE_MEDIUM = 60f;
    private static final float REFRESH_STATE_HIGH = 90f;

    private static final String REFRESH_MEDIUM = "refresh.medium=";
    private static final String REFRESH_HIGH = "refresh.high=";

    private static boolean isAppInList = false;
    private static float defaultMaxRate;
    private static float defaultMinRate;

    private SharedPreferences mSharedPrefs;

    protected RefreshUtils(Context context) {
        mSharedPrefs = PreferenceManager.getDefaultSharedPreferences(context);
        mContext = context;
    }

    public static void initialize(Context context) {
        defaultMaxRate = Settings.System.getFloat(context.getContentResolver(), KEY_PEAK_REFRESH_RATE, REFRESH_STATE_DEFAULT);
        defaultMinRate = Settings.System.getFloat(context.getContentResolver(), KEY_MIN_REFRESH_RATE, REFRESH_STATE_DEFAULT);

        if (isServiceEnabled(context))
            startService(context);
        else
            setDefaultRefreshRate(context);
    }

    public static void startService(Context context) {
        context.startServiceAsUser(new Intent(context, RefreshService.class),
                UserHandle.CURRENT);
        PreferenceManager.getDefaultSharedPreferences(context).edit().putString(REFRESH_SERVICE, "true").apply();
    }

    protected static void stopService(Context context) {
        context.stopService(new Intent(context, RefreshService.class));
        PreferenceManager.getDefaultSharedPreferences(context).edit().putString(REFRESH_SERVICE, "false").apply();
    }

    protected static boolean isServiceEnabled(Context context) {
        return true;
    }

    private void writeValue(String profiles) {
        mSharedPrefs.edit().putString(REFRESH_CONTROL, profiles).apply();
    }

    private String getValue() {
        String value = mSharedPrefs.getString(REFRESH_CONTROL, null);

        if (value == null || value.isEmpty()) {
            value = REFRESH_MEDIUM + ":" + REFRESH_HIGH;
            writeValue(value);
        }
        return value;
    }

    protected void writePackage(String packageName, int mode) {
        String value = getValue();
        value = value.replace(packageName + ",", "");
        String[] modes = value.split(":");
        String finalString;

        switch (mode) {
            case STATE_MEDIUM:
                modes[0] = modes[0] + packageName + ",";
                break;
            case STATE_HIGH:
                modes[1] = modes[1] + packageName + ",";
                break;
        }

        finalString = modes[0] + ":" + modes[1];

        writeValue(finalString);
    }

    protected int getStateForPackage(String packageName) {
        String value = getValue();
        String[] modes = value.split(":");
        int state = STATE_DEFAULT;
        if (modes[0].contains(packageName + ",")) {
            state = STATE_MEDIUM;
        } else if (modes[1].contains(packageName + ",")) {
            state = STATE_HIGH;
        }
        return state;
    }

    protected static void setDefaultRefreshRate(Context context) {
        Settings.System.putFloat(context.getContentResolver(), KEY_PEAK_REFRESH_RATE, defaultMaxRate);
        Settings.System.putFloat(context.getContentResolver(), KEY_MIN_REFRESH_RATE, defaultMinRate);
    }

    protected void setRefreshRate(String packageName) {
        String value = getValue();
        String modes[];

        if (!isAppInList) {
            defaultMaxRate = Settings.System.getFloat(mContext.getContentResolver(), KEY_PEAK_REFRESH_RATE, REFRESH_STATE_DEFAULT);
            defaultMinRate = Settings.System.getFloat(mContext.getContentResolver(), KEY_MIN_REFRESH_RATE, REFRESH_STATE_DEFAULT);
        }

        float minrate = defaultMinRate;
        float maxrate = defaultMaxRate;

        if (value != null) {
            modes = value.split(":");
            if (modes[0].contains(packageName + ",")) {
                maxrate = REFRESH_STATE_MEDIUM;
                if (minrate > maxrate) {
                    minrate = maxrate;
                }
                isAppInList = true;
            } else if (modes[1].contains(packageName + ",")) {
                maxrate = REFRESH_STATE_HIGH;
                if (minrate > maxrate) {
                    minrate = maxrate;
                }
                isAppInList = true;
            } else {
                isAppInList = false;
            }
        }
        Settings.System.putFloat(mContext.getContentResolver(), KEY_PEAK_REFRESH_RATE, maxrate);
        Settings.System.putFloat(mContext.getContentResolver(), KEY_MIN_REFRESH_RATE, minrate);
    }
}
