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

import android.annotation.NonNull;
import android.app.AlertDialog;
import android.app.Service;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.res.Resources;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.hardware.camera2.CameraManager;
import android.media.AudioAttributes;
import android.media.SoundPool;
import android.os.Handler;
import android.os.IBinder;
import android.os.Message;
import android.os.RemoteException;
import android.os.SystemClock;
import android.os.UserHandle;
import android.util.Log;
import android.view.WindowManager;
import android.widget.Toast;

import org.lineageos.settings.R;
import org.lineageos.settings.sensors.ProximitySensor;
import org.lineageos.settings.sensors.SensorsUtils;
import org.lineageos.settings.utils.FileUtils;

import vendor.xiaomi.hardware.motor.V1_0.IMotor;
import vendor.xiaomi.hardware.motor.V1_0.IMotorCallback;
import vendor.xiaomi.hardware.motor.V1_0.MotorEvent;

import java.util.NoSuchElementException;

public class PopupCameraService extends Service implements Handler.Callback {

    private static final String TAG = "PopupCameraService";
    private static final boolean DEBUG = false;

    private IMotor mMotor = null;

    private final Object mLock = new Object();
    private boolean mMotorBusy = false;
    private long mClosedEvent;
    private long mOpenEvent;
    private Handler mHandler = new Handler(this);
    private boolean mMotorCalibrating = false;
    private boolean mErrorDialogShowing;

    private PopupCameraPreferences mPopupCameraPreferences;

    private SensorManager mSensorManager;
    private Sensor mFreeFallSensor;
    private ProximitySensor mProximitySensor;

    private int[] mSounds;
    private SoundPool mSoundPool;

    private IMotorCallback mMotorCallback = new IMotorCallback.Stub() {
        @Override
        public void onNotify(MotorEvent event) {
            int status = event.vaalue;
            int cookie = event.cookie;
            if (DEBUG)
                Log.d(TAG, "onNotify: cookie=" + cookie + ",status=" + status);
            synchronized (mLock) {
                if (status == Constants.MOTOR_STATUS_CALIB_OK ||
                        status == Constants.MOTOR_STATUS_CALIB_ERROR) {
                    mMotorCalibrating = false;
                    showCalibrationResult(status);
                } else if (status == Constants.MOTOR_STATUS_PRESSED) {
                    updateMotor(Constants.CLOSE_CAMERA_STATE);
                    goBackHome();
                } else if (status == Constants.MOTOR_STATUS_POPUP_JAMMED ||
                        status == Constants.MOTOR_STATUS_TAKEBACK_JAMMED) {
                    showErrorDialog();
                }
            }
        }
    };

    private CameraManager.AvailabilityCallback mAvailabilityCallback =
            new CameraManager.AvailabilityCallback() {
        @Override
        public void onCameraClosed(@NonNull String cameraId) {
            super.onCameraClosed(cameraId);
            if (cameraId.equals(Constants.FRONT_CAMERA_ID)) {
                mClosedEvent = SystemClock.elapsedRealtime();
                if (SystemClock.elapsedRealtime() - mOpenEvent <
                        Constants.CAMERA_EVENT_DELAY_TIME &&
                        mHandler.hasMessages(Constants.MSG_CAMERA_OPEN)) {
                    mHandler.removeMessages(Constants.MSG_CAMERA_OPEN);
                }
                mHandler.sendEmptyMessageDelayed(Constants.MSG_CAMERA_CLOSED,
                        Constants.CAMERA_EVENT_DELAY_TIME);
            }
        }

        @Override
        public void onCameraOpened(@NonNull String cameraId, @NonNull String packageId) {
            super.onCameraOpened(cameraId, packageId);
            if (cameraId.equals(Constants.FRONT_CAMERA_ID)) {
                mOpenEvent = SystemClock.elapsedRealtime();
                if (SystemClock.elapsedRealtime() - mClosedEvent <
                        Constants.CAMERA_EVENT_DELAY_TIME &&
                        mHandler.hasMessages(Constants.MSG_CAMERA_CLOSED)) {
                    mHandler.removeMessages(Constants.MSG_CAMERA_CLOSED);
                }
                mHandler.sendEmptyMessageDelayed(Constants.MSG_CAMERA_OPEN,
                        Constants.CAMERA_EVENT_DELAY_TIME);
            }
        }
    };

    private SensorEventListener mFreeFallListener = new SensorEventListener() {
        @Override
        public void onSensorChanged(SensorEvent event) {
            if (event.values[0] == 2.0f) {
                updateMotor(Constants.CLOSE_CAMERA_STATE);
                goBackHome();
            }
        }

        @Override
        public void onAccuracyChanged(Sensor sensor, int accuracy) {
        }
    };

    // Service
    @Override
    public void onCreate() {
        CameraManager cameraManager = getSystemService(CameraManager.class);
        cameraManager.registerAvailabilityCallback(mAvailabilityCallback, null);

        mSensorManager = getSystemService(SensorManager.class);
        mFreeFallSensor = SensorsUtils.getSensor(mSensorManager, "xiaomi.sensor.free_fall");
        mProximitySensor = new ProximitySensor(this);

        mPopupCameraPreferences = new PopupCameraPreferences(this);

        mSoundPool = new SoundPool.Builder()
                .setMaxStreams(1)
                .setAudioAttributes(new AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_ASSISTANCE_SONIFICATION)
                        .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                        .setFlags(AudioAttributes.FLAG_AUDIBILITY_ENFORCED)
                        .build())
                .build();
        String[] soundNames = getResources().getStringArray(R.array.popupcamera_effects_names);
        mSounds = new int[soundNames.length];
        for (int i = 0; i < soundNames.length; i++) {
            mSounds[i] = mSoundPool.load(Constants.POPUP_SOUND_PATH + soundNames[i], 1);
        }

        IMotor motor = getMotor();
        if (motor == null) {
            return;
        }

        try {
            int status = motor.getMotorStatus();
            if (status == Constants.MOTOR_STATUS_POPUP_OK ||
                    status == Constants.MOTOR_STATUS_POPUP_JAMMED ||
                    status == Constants.MOTOR_STATUS_TAKEBACK_JAMMED) {
                mHandler.sendEmptyMessage(Constants.MSG_CAMERA_CLOSED);
            }
        } catch (RemoteException e) {
            // Do nothing
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (DEBUG)
            Log.d(TAG, "Starting service");
        IMotor motor = getMotor();
        if (motor == null) {
            return START_STICKY;
        }
        mProximitySensor.enable();
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        if (DEBUG)
            Log.d(TAG, "Destroying service");
        IMotor motor = getMotor();
        if (motor == null) {
            return;
        }
        mProximitySensor.disable();
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    // Handler.Callback
    @Override
    public boolean handleMessage(Message msg) {
        switch (msg.what) {
            case Constants.MSG_CAMERA_CLOSED: {
                updateMotor(Constants.CLOSE_CAMERA_STATE);
            }
            break;
            case Constants.MSG_CAMERA_OPEN: {
                updateMotor(Constants.OPEN_CAMERA_STATE);
            }
            break;
        }
        return true;
    }

    static public IMotor getMotorService() {
        IMotor motor = null;
        try {
            motor = IMotor.getService();
        } catch (NoSuchElementException | RemoteException e) {
            // Do nothing
        }
        return motor;
    }

    public IMotor getMotor() {
        if (mMotor == null) {
            try {
                mMotor = getMotorService();
                if (mMotor != null) {
                    mMotor.setMotorCallback(mMotorCallback);
                    mMotor.asBinder().linkToDeath((cookie) -> {
                        mMotor = null;
                    }, 0);
                }
            } catch (RemoteException e) {
                // Do nothing
            }
        }
        return mMotor;
    }

    private void updateMotor(String cameraState) {
        IMotor motor = getMotor();
        if (motor == null || mProximitySensor.getSawNear()) {
            return;
        }
        final Runnable r = () -> {
            mMotorBusy = true;
            try {
                int status = motor.getMotorStatus();
                if (DEBUG)
                    Log.d(TAG, "updateMotor: status=" + status);
                if (cameraState.equals(Constants.OPEN_CAMERA_STATE) &&
                        motor.getMotorStatus() == Constants.MOTOR_STATUS_TAKEBACK_OK) {
                    lightUp();
                    playSoundEffect(Constants.OPEN_CAMERA_STATE);
                    motor.popupMotor(1);
                    mSensorManager.registerListener(mFreeFallListener, mFreeFallSensor,
                            SensorManager.SENSOR_DELAY_NORMAL);
                } else if (cameraState.equals(Constants.CLOSE_CAMERA_STATE) &&
                        motor.getMotorStatus() == Constants.MOTOR_STATUS_POPUP_OK) {
                    lightUp();
                    playSoundEffect(Constants.CLOSE_CAMERA_STATE);
                    motor.takebackMotor(1);
                    mSensorManager.unregisterListener(mFreeFallListener, mFreeFallSensor);
                } else {
                    mMotorBusy = false;
                    if (status == Constants.MOTOR_STATUS_REQUEST_CALIB ||
                            status == Constants.MOTOR_STATUS_POPUP_JAMMED ||
                            status == Constants.MOTOR_STATUS_TAKEBACK_JAMMED ||
                            status == Constants.MOTOR_STATUS_CALIB_ERROR) {
                        showErrorDialog();
                    }
                    return;
                }
            } catch (RemoteException e) {
                // Do nothing
            }
            mHandler.postDelayed(() -> mMotorBusy = false, 1200);
        };

        if (mMotorBusy) {
            mHandler.postDelayed(new Runnable() {
                @Override
                public void run() {
                    if (mMotorBusy) {
                        mHandler.postDelayed(this, 100);
                    } else {
                        mHandler.post(r);
                    }
                }
            }, 100);
        } else {
            mHandler.post(r);
        }
    }

    private void lightUp() {
        if (mPopupCameraPreferences.isLedAllowed()) {
            FileUtils.writeLine(Constants.RED_LED_PATH, "1");
            FileUtils.writeLine(Constants.GREEN_LED_PATH, "1");
            FileUtils.writeLine(Constants.BLUE_LED_PATH, "1");
            FileUtils.writeLine(Constants.RED_RIGHT_LED_PATH, "1");
            FileUtils.writeLine(Constants.GREEN_RIGHT_LED_PATH, "1");
            FileUtils.writeLine(Constants.BLUE_RIGHT_LED_PATH, "1");

            mHandler.postDelayed(() -> {
                FileUtils.writeLine(Constants.RED_LED_PATH, "0");
                FileUtils.writeLine(Constants.GREEN_LED_PATH, "0");
                FileUtils.writeLine(Constants.BLUE_LED_PATH, "0");
                FileUtils.writeLine(Constants.RED_RIGHT_LED_PATH, "0");
                FileUtils.writeLine(Constants.GREEN_RIGHT_LED_PATH, "0");
                FileUtils.writeLine(Constants.BLUE_RIGHT_LED_PATH, "0");
            }, 2300);
        }
    }

    private void calibrateMotor() {
        synchronized (mLock) {
            IMotor motor = getMotor();
            if (mMotorCalibrating || motor == null)
                return;
            try {
                mMotorCalibrating = true;
                motor.calibration();
            } catch (RemoteException e) {
                // Do nothing
            }
        }
    }

    private void showCalibrationResult(int status) {
        mHandler.post(() -> {
            Toast.makeText(PopupCameraService.this,
                    status == Constants.MOTOR_STATUS_CALIB_OK
                            ? R.string.popup_camera_calibrate_success
                            : R.string.popup_camera_calibrate_failed,
                    Toast.LENGTH_LONG)
                    .show();
        });
    }

    private void showErrorDialog() {
        if (mErrorDialogShowing) {
            return;
        }
        mErrorDialogShowing = true;
        goBackHome();
        mHandler.post(() -> {
            Resources res = getResources();
            String cameraState = "-1";
            int dialogMessageResId = cameraState.equals(Constants.CLOSE_CAMERA_STATE)
                    ? R.string.popup_camera_takeback_failed_times_calibrate
                    : R.string.popup_camera_popup_failed_times_calibrate;
            AlertDialog alertDialog = new AlertDialog.Builder(this, R.style.SystemAlertDialogTheme)
                    .setTitle(res.getString(R.string.popup_camera_tip))
                    .setMessage(res.getString(dialogMessageResId))
                    .setPositiveButton(
                            res.getString(R.string.popup_camera_calibrate_now),
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
        int soundEffect = Integer.parseInt(mPopupCameraPreferences.getSoundEffect());
        if (soundEffect != -1) {
            if (state.equals(Constants.CLOSE_CAMERA_STATE)) {
                soundEffect++;
            }
            mSoundPool.play(mSounds[soundEffect], 1.0f, 1.0f, 0, 0, 1.0f);
        }
    }

    public void goBackHome() {
        Intent homeIntent = new Intent(Intent.ACTION_MAIN);
        homeIntent.addCategory(Intent.CATEGORY_HOME);
        homeIntent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        startActivityAsUser(homeIntent, null, UserHandle.CURRENT);
    }
}
