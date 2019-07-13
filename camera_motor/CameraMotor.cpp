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

#define LOG_TAG "CameraMotorService"

#include "CameraMotor.h"
#include <android-base/logging.h>
#include <hidl/HidlTransportSupport.h>

#include <misc/drv8846.h>

#define CAMERA_ID_FRONT "1"
#define MOTOR_DEV_PATH "/dev/drv8846_dev"

namespace vendor {
namespace lineage {
namespace camera {
namespace motor {
namespace V1_0 {
namespace implementation {

CameraMotor::CameraMotor() {
    motor_fd_ = android::base::unique_fd(open(MOTOR_DEV_PATH, O_RDWR));
}

Return<void> CameraMotor::onConnect(const hidl_string& cameraId) {
    if (cameraId == CAMERA_ID_FRONT) {
        LOG(INFO) << "Camera is uprising.";
        uint8_t arg = UP;
        ioctl(motor_fd_.get(), MOTOR_IOC_SET_AUTORUN, &arg);
    }

    return Void();
}

Return<void> CameraMotor::onDisconnect(const hidl_string& cameraId) {
    if (cameraId == CAMERA_ID_FRONT) {
        LOG(INFO) << "Camera is descending";
        uint8_t arg = DOWN;
        ioctl(motor_fd_.get(), MOTOR_IOC_SET_AUTORUN, &arg);
    }

    return Void();
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace motor
}  // namespace camera
}  // namespace lineage
}  // namespace vendor
