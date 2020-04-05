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
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.PowerManager;
import android.os.PowerManager.WakeLock;
import android.os.SystemClock;
import android.util.Log;
import android.view.KeyEvent;

import com.android.internal.os.DeviceKeyHandler;

import org.lineageos.settings.utils.ProximitySensor;

public class FodKeyHandler implements DeviceKeyHandler {
    private static final String TAG = "FodKeyHandler";
    private static final boolean DEBUG = false;

    private static final int KEY_FOD_GESTURE_DOWN = 745;

    protected final Context mContext;
    private final PowerManager mPowerManager;
    private final WakeLock mFodWakeLock;

    // Proximity sensor
    private SensorManager mSensorManager;
    private ProximitySensor mProximitySensor;
    private boolean mProximityNear = false;

    public FodKeyHandler(Context context) {
        mContext = context;
        mPowerManager = (PowerManager) context.getSystemService(Context.POWER_SERVICE);
        mFodWakeLock = mPowerManager.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK,
                "ScreenOffFODWakeLock");
        mSensorManager = mContext.getSystemService(SensorManager.class);
        mProximitySensor = new ProximitySensor(mContext, mSensorManager, mProximityListener);
        IntentFilter filter = new IntentFilter();
        filter.addAction(Intent.ACTION_SCREEN_ON);
        filter.addAction(Intent.ACTION_SCREEN_OFF);
        mContext.registerReceiver(mIntentReceiver, filter);
    }

    private BroadcastReceiver mIntentReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            final String action = intent.getAction();
            if (Intent.ACTION_SCREEN_ON.equals(action)) {
                if (DEBUG) Log.d(TAG, "ACTION_SCREEN_ON received, disabling proximity listener");
                setProximitySensor(false);
            }else if (Intent.ACTION_SCREEN_OFF.equals(action)) {
                if (DEBUG) Log.d(TAG, "ACTION_SCREEN_OFF received");
                if (FodScreenOffHandlerImpl.isDaemonRunning()){
                    if (DEBUG) Log.d(TAG, "Daemon running, starting proximity listener");
                    setProximitySensor(true);
                }
            }
        }
    };

    private void setProximitySensor(boolean enabled) {
        if (mProximitySensor == null) return;
        if (enabled) {
            if (DEBUG) Log.d(TAG, "Proximity sensor enabling");
            mProximitySensor.enable();
        } else {
            if (DEBUG) Log.d(TAG, "Proximity sensor disabling");
            mProximitySensor.disable();
        }
    }

    private ProximitySensor.ProximityListener mProximityListener =
            new ProximitySensor.ProximityListener() {
        public void onEvent(boolean isNear, long timestamp) {
            mProximityNear = isNear;
            if (DEBUG) Log.d(TAG, "Proximity sensor: isNear " + mProximityNear);
        }
        public void onInit(boolean isNear, long timestamp) {
            if (DEBUG) Log.d(TAG, "Proximity sensor init : " + isNear);
            mProximityNear = isNear;
        }
    };

    private void wakeUp() {
        mFodWakeLock.acquire(3000);
        mPowerManager.wakeUp(SystemClock.uptimeMillis(), "screen-off-fod");
    }

    public KeyEvent handleKeyEvent(KeyEvent event) {
        if (event.getAction() != KeyEvent.ACTION_UP) {
            return event;
        }
        if (event.getScanCode() == KEY_FOD_GESTURE_DOWN && !mProximityNear){
            wakeUp();
            return null;
        }
        return event;
    }
}