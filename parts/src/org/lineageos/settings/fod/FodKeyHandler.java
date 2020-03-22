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

package org.lineageos.settings.fod;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.provider.Settings;
import android.os.PowerManager;
import android.os.PowerManager.WakeLock;
import android.os.SystemClock;
import android.util.Log;
import android.view.KeyEvent;

import com.android.internal.os.DeviceKeyHandler;

public class FodKeyHandler implements DeviceKeyHandler {
    private static final String TAG = FodKeyHandler.class.getSimpleName();

    private static final boolean DEBUG = true;

    private static final int KEY_FOD_GESTURE_DOWN = 745;

    protected final Context mContext;
    private final PowerManager mPowerManager;
    private final WakeLock mFodWakeLock;

    private boolean mInteractive = true;
    private BroadcastReceiver mScreenStateReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (intent.getAction().equals(Intent.ACTION_SCREEN_ON)) {
                mInteractive = true;
            } else if (intent.getAction().equals(Intent.ACTION_SCREEN_OFF)) {
                mInteractive = false;
            }
        }
    };

    public FodKeyHandler(Context context) {
        mContext = context;
        mPowerManager = (PowerManager) context.getSystemService(Context.POWER_SERVICE);
        mFodWakeLock = mPowerManager.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK,
                "ScreenOffFODWakeLock");
        IntentFilter screenStateFilter = new IntentFilter(Intent.ACTION_SCREEN_ON);
        screenStateFilter.addAction(Intent.ACTION_SCREEN_OFF);
        context.registerReceiver(mScreenStateReceiver, screenStateFilter);
    }


    private boolean hasSetupCompleted() {
        return Settings.Secure.getInt(mContext.getContentResolver(),
                Settings.Secure.USER_SETUP_COMPLETE, 0) != 0;
    }

    private void wakeUp() {
        mFodWakeLock.acquire(3000);
        mPowerManager.wakeUp(SystemClock.uptimeMillis(), "screen-off-fod");
    }

    private void handleFODScreenOff() {
        if (mInteractive) {
            return;
        }

        wakeUp();
    }

    public KeyEvent handleKeyEvent(KeyEvent event) {
        if (!hasSetupCompleted()) {
            return event;
        }

        if (event.getAction() != KeyEvent.ACTION_UP) {
            return event;
        }

        int scanCode = event.getScanCode();

        switch (scanCode) {
            case KEY_FOD_GESTURE_DOWN:
                handleFODScreenOff();
                break;
            default:
                return event;
        }

        return null;
    }
}