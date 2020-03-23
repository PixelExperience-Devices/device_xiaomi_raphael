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

import android.content.Context;
import android.os.PowerManager;
import android.os.PowerManager.WakeLock;
import android.os.SystemClock;
import android.view.KeyEvent;

import com.android.internal.os.DeviceKeyHandler;

public class FodKeyHandler implements DeviceKeyHandler {
    private static final int KEY_FOD_GESTURE_DOWN = 745;

    protected final Context mContext;
    private final PowerManager mPowerManager;
    private final WakeLock mFodWakeLock;

    public FodKeyHandler(Context context) {
        mContext = context;
        mPowerManager = (PowerManager) context.getSystemService(Context.POWER_SERVICE);
        mFodWakeLock = mPowerManager.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK,
                "ScreenOffFODWakeLock");
    }

    private void wakeUp() {
        mFodWakeLock.acquire(3000);
        mPowerManager.wakeUp(SystemClock.uptimeMillis(), "screen-off-fod");
    }

    public KeyEvent handleKeyEvent(KeyEvent event) {
        if (event.getAction() != KeyEvent.ACTION_UP) {
            return event;
        }
        if (event.getScanCode() == KEY_FOD_GESTURE_DOWN){
            wakeUp();
            return null;
        }
        return event;
    }
}