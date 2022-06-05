/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * *    * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <array>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include <aidl/android/hardware/power/IPower.h>
#include <aidl/google/hardware/power/extension/pixel/IPowerExt.h>
#include <android/hardware/thermal/2.0/IThermal.h>

#include "utils/config_parser.h"
#include "utils/power_files.h"
#include "utils/thermal_files.h"
#include "utils/thermal_watcher.h"

namespace android {
namespace hardware {
namespace thermal {
namespace V2_0 {
namespace implementation {

using ::aidl::android::hardware::power::IPower;
using ::aidl::google::hardware::power::extension::pixel::IPowerExt;
using ::android::hardware::hidl_vec;
using ::android::hardware::thermal::V1_0::CpuUsage;
using ::android::hardware::thermal::V2_0::CoolingType;
using ::android::hardware::thermal::V2_0::IThermal;
using CoolingDevice_1_0 = ::android::hardware::thermal::V1_0::CoolingDevice;
using CoolingDevice_2_0 = ::android::hardware::thermal::V2_0::CoolingDevice;
using Temperature_1_0 = ::android::hardware::thermal::V1_0::Temperature;
using Temperature_2_0 = ::android::hardware::thermal::V2_0::Temperature;
using TemperatureType_1_0 = ::android::hardware::thermal::V1_0::TemperatureType;
using TemperatureType_2_0 = ::android::hardware::thermal::V2_0::TemperatureType;
using ::android::hardware::thermal::V2_0::TemperatureThreshold;
using ::android::hardware::thermal::V2_0::ThrottlingSeverity;

using NotificationCallback = std::function<void(const Temperature_2_0 &t)>;
using NotificationTime = std::chrono::time_point<std::chrono::steady_clock>;
using CdevRequestStatus = std::unordered_map<std::string, int>;

// Get thermal_zone type
bool getThermalZoneTypeById(int tz_id, std::string *);

struct SensorStatus {
    ThrottlingSeverity severity;
    ThrottlingSeverity prev_hot_severity;
    ThrottlingSeverity prev_cold_severity;
    ThrottlingSeverity prev_hint_severity;
    boot_clock::time_point last_update_time;
    std::unordered_map<std::string, int> pid_request_map;
    std::unordered_map<std::string, int> hard_limit_request_map;
    float err_integral;
    float prev_err;
};

class PowerHalService {
  public:
    PowerHalService();
    ~PowerHalService() = default;
    bool connect();
    bool isAidlPowerHalExist() { return power_hal_aidl_exist_; }
    bool isModeSupported(const std::string &type, const ThrottlingSeverity &t);
    bool isPowerHalConnected() { return power_hal_aidl_ != nullptr; }
    bool isPowerHalExtConnected() { return power_hal_ext_aidl_ != nullptr; }
    void setMode(const std::string &type, const ThrottlingSeverity &t, const bool &enable);

  private:
    bool power_hal_aidl_exist_;
    std::shared_ptr<IPower> power_hal_aidl_;
    std::shared_ptr<IPowerExt> power_hal_ext_aidl_;
    std::mutex lock_;
};

class ThermalHelper {
  public:
    explicit ThermalHelper(const NotificationCallback &cb);
    ~ThermalHelper() = default;

    bool fillTemperatures(hidl_vec<Temperature_1_0> *temperatures) const;
    bool fillCurrentTemperatures(bool filterType, bool filterCallback, TemperatureType_2_0 type,
                                 hidl_vec<Temperature_2_0> *temperatures) const;
    bool fillTemperatureThresholds(bool filterType, TemperatureType_2_0 type,
                                   hidl_vec<TemperatureThreshold> *thresholds) const;
    bool fillCurrentCoolingDevices(bool filterType, CoolingType type,
                                   hidl_vec<CoolingDevice_2_0> *coolingdevices) const;
    bool fillCpuUsages(hidl_vec<CpuUsage> *cpu_usages) const;

    // Dissallow copy and assign.
    ThermalHelper(const ThermalHelper &) = delete;
    void operator=(const ThermalHelper &) = delete;

    bool isInitializedOk() const { return is_initialized_; }

    // Read the temperature of a single sensor.
    bool readTemperature(std::string_view sensor_name, Temperature_1_0 *out,
                         bool is_virtual_sensor = false) const;
    bool readTemperature(
            std::string_view sensor_name, Temperature_2_0 *out,
            std::pair<ThrottlingSeverity, ThrottlingSeverity> *throtting_status = nullptr,
            bool is_virtual_sensor = false) const;
    bool readTemperatureThreshold(std::string_view sensor_name, TemperatureThreshold *out) const;
    // Read the value of a single cooling device.
    bool readCoolingDevice(std::string_view cooling_device, CoolingDevice_2_0 *out) const;
    // Get SensorInfo Map
    const std::unordered_map<std::string, SensorInfo> &GetSensorInfoMap() const {
        return sensor_info_map_;
    }
    // Get CdevInfo Map
    const std::unordered_map<std::string, CdevInfo> &GetCdevInfoMap() const {
        return cooling_device_info_map_;
    }
    // Get PowerRailInfo Map
    const std::unordered_map<std::string, PowerRailInfo> &GetPowerRailInfoMap() const {
        return power_rail_info_map_;
    }
    // Get SensorStatus Map
    const std::unordered_map<std::string, SensorStatus> &GetSensorStatusMap() const {
        std::shared_lock<std::shared_mutex> _lock(sensor_status_map_mutex_);
        return sensor_status_map_;
    }
    // Get CdevStatus Map
    const std::unordered_map<std::string, CdevRequestStatus> &GetCdevStatusMap() const {
        std::shared_lock<std::shared_mutex> _lock(cdev_status_map_mutex_);
        return cdev_status_map_;
    }
    // Get ThrottlingRelease Map
    const std::unordered_map<std::string, CdevReleaseStatus> &GetThrottlingReleaseMap() const {
        return power_files_.GetThrottlingReleaseMap();
    }

    // Get PowerStatus Map
    const std::unordered_map<std::string, PowerStatusMap> &GetPowerStatusMap() const {
        return power_files_.GetPowerStatusMap();
    }

    void sendPowerExtHint(const Temperature_2_0 &t);
    bool isAidlPowerHalExist() { return power_hal_service_.isAidlPowerHalExist(); }
    bool isPowerHalConnected() { return power_hal_service_.isPowerHalConnected(); }
    bool isPowerHalExtConnected() { return power_hal_service_.isPowerHalExtConnected(); }

  private:
    bool initializeSensorMap(const std::unordered_map<std::string, std::string> &path_map);
    bool initializeCoolingDevices(const std::unordered_map<std::string, std::string> &path_map);
    void setMinTimeout(SensorInfo *sensor_info);
    void initializeTrip(const std::unordered_map<std::string, std::string> &path_map,
                        std::set<std::string> *monitored_sensors, bool thermal_genl_enabled);

    // For thermal_watcher_'s polling thread, return the sleep interval
    std::chrono::milliseconds thermalWatcherCallbackFunc(
            const std::set<std::string> &uevent_sensors);
    // Return hot and cold severity status as std::pair
    std::pair<ThrottlingSeverity, ThrottlingSeverity> getSeverityFromThresholds(
        const ThrottlingArray &hot_thresholds, const ThrottlingArray &cold_thresholds,
        const ThrottlingArray &hot_hysteresis, const ThrottlingArray &cold_hysteresis,
        ThrottlingSeverity prev_hot_severity, ThrottlingSeverity prev_cold_severity,
        float value) const;
    bool checkVirtualSensor(std::string_view sensor_name, std::string *temp) const;

    // Return the target state of PID algorithm
    size_t getTargetStateOfPID(const SensorInfo &sensor_info, const SensorStatus &sensor_status);
    // Return the power budget which is computed by PID algorithm
    float pidPowerCalculator(const Temperature_2_0 &temp, const SensorInfo &sensor_info,
                             SensorStatus *sensor_status,
                             const std::chrono::milliseconds time_elapsed_ms, size_t target_state);
    bool connectToPowerHal();
    void updateSupportedPowerHints();
    bool requestCdevByPower(std::string_view sensor_name, SensorStatus *sensor_status,
                            const SensorInfo &sensor_info, float total_power_budget,
                            size_t target_state);
    void requestCdevBySeverity(std::string_view sensor_name, SensorStatus *sensor_status,
                               const SensorInfo &sensor_info);
    void computeCoolingDevicesRequest(std::string_view sensor_name, const SensorInfo &sensor_info,
                                      const SensorStatus &sensor_status,
                                      std::vector<std::string> *cooling_devices_to_update);
    void updateCoolingDevices(const std::vector<std::string> &cooling_devices_to_update);
    sp<ThermalWatcher> thermal_watcher_;
    PowerFiles power_files_;
    ThermalFiles thermal_sensors_;
    ThermalFiles cooling_devices_;
    bool is_initialized_;
    const NotificationCallback cb_;
    std::unordered_map<std::string, CdevInfo> cooling_device_info_map_;
    std::unordered_map<std::string, SensorInfo> sensor_info_map_;
    std::unordered_map<std::string, PowerRailInfo> power_rail_info_map_;
    std::unordered_map<std::string, std::map<ThrottlingSeverity, ThrottlingSeverity>>
            supported_powerhint_map_;
    PowerHalService power_hal_service_;

    mutable std::shared_mutex sensor_status_map_mutex_;
    std::unordered_map<std::string, SensorStatus> sensor_status_map_;
    mutable std::shared_mutex cdev_status_map_mutex_;
    std::unordered_map<std::string, CdevRequestStatus> cdev_status_map_;
};

}  // namespace implementation
}  // namespace V2_0
}  // namespace thermal
}  // namespace hardware
}  // namespace android
