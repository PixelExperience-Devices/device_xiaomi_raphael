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

import android.app.AlertDialog;
import android.app.Service;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.res.Resources;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.media.AudioManager;
import android.media.Ringtone;
import android.media.RingtoneManager;
import android.net.Uri;
import android.os.IBinder;
import android.os.Handler;
import android.os.SystemClock;
import android.os.UserHandle;
import android.util.Log;
import android.view.WindowManager;
import android.widget.Toast;

import java.util.List;

import org.lineageos.settings.R;
import org.lineageos.settings.utils.FileUtils;
import vendor.xiaomi.hardware.motor.V1_0.IMotor;
import vendor.xiaomi.hardware.motor.V1_0.IMotorCallback;
import vendor.xiaomi.hardware.motor.V1_0.MotorEvent;

public class PopupCameraService extends Service {

    private static final String TAG = "PopupCameraService";
    private static final boolean DEBUG = false;
    private static final String closeCameraState = "0";
    private static final String openCameraState = "1";
    private static String mCameraState = "-1";

    private IMotor mMotor = null;
    private IMotorCallback mMotorStatusCallback;
    private final Object mLock = new Object();
    private boolean mMotorBusy = false;
    private boolean mMotorCalibrating = false;

    private SensorManager mSensorManager;
    private Sensor mFreeFallSensor;
    private static final int FREE_FALL_SENSOR_ID = 33171042;

    private static final String GREEN_LED_PATH = "/sys/class/leds/green/brightness";
    private static final String BLUE_LED_PATH = "/sys/class/leds/blue/brightness";

    private static Handler mHandler = new Handler();

    private PopupCameraPreferences mPopupCameraPreferences;

    // Motor status
    private static final int MOTOR_STATUS_POPUP_OK = 11;
    private static final int MOTOR_STATUS_POPUP_JAMMED = 12;
    private static final int MOTOR_STATUS_TAKEBACK_OK = 13;
    private static final int MOTOR_STATUS_TAKEBACK_JAMMED = 14;
    private static final int MOTOR_STATUS_PRESSED = 15;
    private static final int MOTOR_STATUS_CALIB_OK = 17;
    private static final int MOTOR_STATUS_CALIB_ERROR = 18;
    private static final int MOTOR_STATUS_REQUEST_CALIB = 19;

    // Error dialog
    private boolean mErrorDialogShowing;

    @Override
    public void onCreate() {
        mSensorManager = this.getSystemService(SensorManager.class);
        mFreeFallSensor = mSensorManager.getDefaultSensor(FREE_FALL_SENSOR_ID);
        registerReceiver();
        try {
            mMotor = IMotor.getService();
            mMotorStatusCallback = new MotorStatusCallback();
            mMotor.setMotorCallback(mMotorStatusCallback);
        } catch(Exception e) {
        }
        mPopupCameraPreferences = new PopupCameraPreferences(this);
    }

    private final class MotorStatusCallback extends IMotorCallback.Stub {
        public MotorStatusCallback() {
        }

        @Override
        public void onNotify(MotorEvent event) {
            int status = event.vaalue;
            int cookie = event.cookie;
            if (DEBUG) Log.d(TAG, "onNotify: cookie=" + cookie + ",status=" + status);
            synchronized (mLock) {
                if (status == MOTOR_STATUS_CALIB_OK || status == MOTOR_STATUS_CALIB_ERROR) {
                    mMotorCalibrating = false;
                    showCalibrationResult(status);
                }else if (status == MOTOR_STATUS_PRESSED) {
                    forceTakeback();
                    goBackHome();
                }else if (status == MOTOR_STATUS_POPUP_JAMMED || status == MOTOR_STATUS_TAKEBACK_JAMMED) {
                    showErrorDialog();
                }
            }
        }
    }

    private void calibrateMotor() {
        synchronized (mLock) {
            if (mMotorCalibrating || mMotor == null) return;
            try {
                mMotorCalibrating = true;
                mMotor.calibration();
            } catch (Exception e) {
            }
        }
    }

    private void forceTakeback(){
        mCameraState = closeCameraState;
        updateMotor();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (DEBUG) Log.d(TAG, "Starting service");
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        if (DEBUG) Log.d(TAG, "Destroying service");
        this.unregisterReceiver(mIntentReceiver);
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    private void registerReceiver() {
        IntentFilter filter = new IntentFilter();
        filter.addAction("android.intent.action.ACTION_SHUTDOWN");
        filter.addAction("android.intent.action.SCREEN_ON");
        filter.addAction("android.intent.action.SCREEN_OFF");
        filter.addAction("android.intent.action.CAMERA_STATUS_CHANGED");
        this.registerReceiver(mIntentReceiver, filter);
    }

    private BroadcastReceiver mIntentReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            final String action = intent.getAction();
            if ("android.intent.action.CAMERA_STATUS_CHANGED".equals(action)) {
               mCameraState = intent.getExtras().getString("android.intent.extra.CAMERA_STATE");
               updateMotor();
            }else if ("android.intent.action.SCREEN_OFF".equals(action)) {
                if (mCameraState.equals(openCameraState)){
                    forceTakeback();
                }
            }
        }
    };

    private void updateMotor() {
        final Runnable r = new Runnable() {
            @Override
            public void run() {
                if (mMotor == null) return;
                mMotorBusy = true;
                try {
                    int status = mMotor.getMotorStatus();
                    if (DEBUG) Log.d(TAG, "updateMotor: status=" + status);
                    if (mMotorCalibrating){
                        mMotorBusy = false;
                        goBackHome();
                        return;
                    }else if (mCameraState.equals(openCameraState) && (status == MOTOR_STATUS_TAKEBACK_OK || status == MOTOR_STATUS_CALIB_OK)) {
                        lightUp();
                        playSoundEffect(openCameraState);
                        mMotor.popupMotor(1);
                        mSensorManager.registerListener(mFreeFallListener, mFreeFallSensor, SensorManager.SENSOR_DELAY_NORMAL);
                    } else if (mCameraState.equals(closeCameraState) && (status == MOTOR_STATUS_POPUP_OK || status == MOTOR_STATUS_CALIB_OK)) {
                        lightUp();
                        playSoundEffect(closeCameraState);
                        mMotor.takebackMotor(1);
                        mSensorManager.unregisterListener(mFreeFallListener, mFreeFallSensor);
                    }else{
                        mMotorBusy = false;
                        if (status == MOTOR_STATUS_REQUEST_CALIB || status == MOTOR_STATUS_POPUP_JAMMED || status == MOTOR_STATUS_TAKEBACK_JAMMED || status == MOTOR_STATUS_CALIB_ERROR){
                            showErrorDialog();
                        }
                        return;
                    }
                } catch(Exception e) {
                }
                mHandler.postDelayed(() -> { mMotorBusy = false; }, 1200);
            }
        };
        if (mMotorBusy){
            mHandler.postDelayed(new Runnable() {
                @Override
                public void run() {
                    if (mMotorBusy){
                        mHandler.postDelayed(this, 100);
                    }else{
                        mHandler.post(r);
                    }
                }
            }, 100);
        }else{
            mHandler.post(r);
        }
    }

    private void showCalibrationResult(int status){
        mHandler.post(() -> {
            Toast.makeText(PopupCameraService.this, status == MOTOR_STATUS_CALIB_OK ?
                    R.string.popup_camera_calibrate_success :
                    R.string.popup_camera_calibrate_failed, Toast.LENGTH_LONG).show();
        });
    }

    private void showErrorDialog(){
        if (mErrorDialogShowing){
            return;
        }
        mErrorDialogShowing = true;
        goBackHome();
        mHandler.post(() -> {
            Resources res = getResources();
            int dialogMessageResId = mCameraState.equals(closeCameraState) ?
                R.string.popup_camera_takeback_falied_times_calibrate :
                R.string.popup_camera_popup_falied_times_calibrate;
            AlertDialog alertDialog = new AlertDialog.Builder(this, R.style.SystemAlertDialogTheme)
                    .setTitle(res.getString(R.string.popup_camera_tip))
                    .setMessage(res.getString(dialogMessageResId))
                    .setPositiveButton(res.getString(R.string.popup_camera_calibrate_now),
                            (dialog, which) -> {
                            calibrateMotor();
                    })
                    .setNegativeButton(res.getString(android.R.string.cancel), null)
                    .create();
            alertDialog.getWindow().setType(WindowManager.LayoutParams.TYPE_SYSTEM_ALERT);
            alertDialog.setCanceledOnTouchOutside(false);
            alertDialog.show();
            alertDialog.setOnDismissListener(new DialogInterface.OnDismissListener() {
                    @Override
                    public void onDismiss(DialogInterface dialogInterface) {
                        mErrorDialogShowing = false;
                    }
                });
        });
    }

    private void playSoundEffect(String state) {
        String soundEffect = mPopupCameraPreferences.getSoundEffect();
        if (!soundEffect.equals("0")){
            String soundPath = "/product/media/audio/ui/popup_" + soundEffect + "_" + (state.equals(openCameraState) ? "up" : "down") + ".ogg";
            final Uri soundUri = Uri.parse("file://" + soundPath);
            if (soundUri != null) {
                final Ringtone sfx = RingtoneManager.getRingtone(this, soundUri);
                if (sfx != null) {
                    sfx.setStreamType(AudioManager.STREAM_SYSTEM);
                    sfx.play();
                }
            }
        }
    }

    private void lightUp() {
        if (mPopupCameraPreferences.isLedAllowed()){
            FileUtils.writeLine(GREEN_LED_PATH, "255");
            FileUtils.writeLine(BLUE_LED_PATH, "255");

            mHandler.postDelayed(new Runnable() {
                @Override
                public void run() {
                    FileUtils.writeLine(GREEN_LED_PATH, "0");
                    FileUtils.writeLine(BLUE_LED_PATH, "0");
                }
            }, 1200);
        }
    }

    private SensorEventListener mFreeFallListener = new SensorEventListener() {
        @Override
        public void onSensorChanged(SensorEvent event) {
            if (event.sensor.getType() == FREE_FALL_SENSOR_ID && event.values[0] == 2.0f) {
                forceTakeback();
                goBackHome();
            }
        }

        @Override
        public void onAccuracyChanged(Sensor sensor, int accuracy) {
        }
    };

    public void goBackHome() {
        Intent homeIntent = new Intent(Intent.ACTION_MAIN);
        homeIntent.addCategory(Intent.CATEGORY_HOME);
        homeIntent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        startActivity(homeIntent);
    }
}
