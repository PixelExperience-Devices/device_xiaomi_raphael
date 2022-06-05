/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include <iterator>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <android/binder_manager.h>
#include <hidl/HidlTransportSupport.h>

#include "thermal-helper.h"

namespace android {
namespace hardware {
namespace thermal {
namespace V2_0 {
namespace implementation {

constexpr std::string_view kCpuOnlineRoot("/sys/devices/system/cpu");
constexpr std::string_view kThermalSensorsRoot("/sys/devices/virtual/thermal");
constexpr std::string_view kCpuUsageFile("/proc/stat");
constexpr std::string_view kCpuOnlineFileSuffix("online");
constexpr std::string_view kCpuPresentFile("/sys/devices/system/cpu/present");
constexpr std::string_view kSensorPrefix("thermal_zone");
constexpr std::string_view kCoolingDevicePrefix("cooling_device");
constexpr std::string_view kThermalNameFile("type");
constexpr std::string_view kSensorPolicyFile("policy");
constexpr std::string_view kSensorTempSuffix("temp");
constexpr std::string_view kSensorTripPointTempZeroFile("trip_point_0_temp");
constexpr std::string_view kSensorTripPointHystZeroFile("trip_point_0_hyst");
constexpr std::string_view kUserSpaceSuffix("user_space");
constexpr std::string_view kCoolingDeviceCurStateSuffix("cur_state");
constexpr std::string_view kCoolingDeviceMaxStateSuffix("max_state");
constexpr std::string_view kCoolingDeviceState2powerSuffix("state2power_table");
constexpr std::string_view kConfigProperty("vendor.thermal.config");
constexpr std::string_view kConfigDefaultFileName("thermal_info_config.json");
constexpr std::string_view kThermalGenlProperty("persist.vendor.enable.thermal.genl");
constexpr std::string_view kThermalDisabledProperty("vendor.disable.thermal.control");

namespace {
using android::base::StringPrintf;
using android::hardware::thermal::V2_0::toString;

/*
 * Pixel don't offline CPU, so std::thread::hardware_concurrency(); should work.
 * However /sys/devices/system/cpu/present is preferred.
 * The file is expected to contain single text line with two numbers %d-%d,
 * which is a range of available cpu numbers, e.g. 0-7 would mean there
 * are 8 cores number from 0 to 7.
 * For Android systems this approach is safer than using cpufeatures, see bug
 * b/36941727.
 */
static int getNumberOfCores() {
    std::string file;
    if (!android::base::ReadFileToString(kCpuPresentFile.data(), &file)) {
        LOG(ERROR) << "Error reading Cpu present file: " << kCpuPresentFile;
        return 0;
    }
    std::vector<std::string> pieces = android::base::Split(file, "-");
    if (pieces.size() != 2) {
        LOG(ERROR) << "Error parsing Cpu present file content: " << file;
        return 0;
    }
    auto min_core = std::stoul(pieces[0]);
    auto max_core = std::stoul(pieces[1]);
    if (max_core < min_core) {
        LOG(ERROR) << "Error parsing Cpu present min and max: " << min_core << " - " << max_core;
        return 0;
    }
    return static_cast<std::size_t>(max_core - min_core + 1);
}
const int kMaxCpus = getNumberOfCores();

void parseCpuUsagesFileAndAssignUsages(hidl_vec<CpuUsage> *cpu_usages) {
    std::string data;
    if (!android::base::ReadFileToString(kCpuUsageFile.data(), &data)) {
        LOG(ERROR) << "Error reading cpu usage file: " << kCpuUsageFile;
        return;
    }

    std::istringstream stat_data(data);
    std::string line;
    while (std::getline(stat_data, line)) {
        if (!line.find("cpu") && isdigit(line[3])) {
            // Split the string using spaces.
            std::vector<std::string> words = android::base::Split(line, " ");
            std::string cpu_name = words[0];
            int cpu_num = std::stoi(cpu_name.substr(3));

            if (cpu_num < kMaxCpus) {
                uint64_t user = std::stoull(words[1]);
                uint64_t nice = std::stoull(words[2]);
                uint64_t system = std::stoull(words[3]);
                uint64_t idle = std::stoull(words[4]);

                // Check if the CPU is online by reading the online file.
                std::string cpu_online_path =
                        StringPrintf("%s/%s/%s", kCpuOnlineRoot.data(), cpu_name.c_str(),
                                     kCpuOnlineFileSuffix.data());
                std::string is_online;
                if (!android::base::ReadFileToString(cpu_online_path, &is_online)) {
                    LOG(ERROR) << "Could not open Cpu online file: " << cpu_online_path;
                    if (cpu_num != 0) {
                        return;
                    }
                    // Some architecture cannot offline cpu0, so assuming it is online
                    is_online = "1";
                }
                is_online = android::base::Trim(is_online);

                (*cpu_usages)[cpu_num].active = user + nice + system;
                (*cpu_usages)[cpu_num].total = user + nice + system + idle;
                (*cpu_usages)[cpu_num].isOnline = (is_online == "1") ? true : false;
            } else {
                LOG(ERROR) << "Unexpected cpu number: " << words[0];
                return;
            }
        }
    }
}

std::unordered_map<std::string, std::string> parseThermalPathMap(std::string_view prefix) {
    std::unordered_map<std::string, std::string> path_map;
    std::unique_ptr<DIR, int (*)(DIR *)> dir(opendir(kThermalSensorsRoot.data()), closedir);
    if (!dir) {
        return path_map;
    }

    // std::filesystem is not available for vendor yet
    // see discussion: aosp/894015
    while (struct dirent *dp = readdir(dir.get())) {
        if (dp->d_type != DT_DIR) {
            continue;
        }

        if (!android::base::StartsWith(dp->d_name, prefix.data())) {
            continue;
        }

        std::string path = android::base::StringPrintf("%s/%s/%s", kThermalSensorsRoot.data(),
                                                       dp->d_name, kThermalNameFile.data());
        std::string name;
        if (!android::base::ReadFileToString(path, &name)) {
            PLOG(ERROR) << "Failed to read from " << path;
            continue;
        }

        path_map.emplace(
                android::base::Trim(name),
                android::base::StringPrintf("%s/%s", kThermalSensorsRoot.data(), dp->d_name));
    }

    return path_map;
}

}  // namespace
PowerHalService::PowerHalService()
    : power_hal_aidl_exist_(true), power_hal_aidl_(nullptr), power_hal_ext_aidl_(nullptr) {
    connect();
}

bool PowerHalService::connect() {
    std::lock_guard<std::mutex> lock(lock_);
    if (!power_hal_aidl_exist_)
        return false;

    if (power_hal_aidl_ != nullptr)
        return true;

    const std::string kInstance = std::string(IPower::descriptor) + "/default";
    ndk::SpAIBinder power_binder = ndk::SpAIBinder(AServiceManager_getService(kInstance.c_str()));
    ndk::SpAIBinder ext_power_binder;

    if (power_binder.get() == nullptr) {
        LOG(ERROR) << "Cannot get Power Hal Binder";
        power_hal_aidl_exist_ = false;
        return false;
    }

    power_hal_aidl_ = IPower::fromBinder(power_binder);

    if (power_hal_aidl_ == nullptr) {
        power_hal_aidl_exist_ = false;
        LOG(ERROR) << "Cannot get Power Hal AIDL" << kInstance.c_str();
        return false;
    }

    if (STATUS_OK != AIBinder_getExtension(power_binder.get(), ext_power_binder.getR()) ||
        ext_power_binder.get() == nullptr) {
        LOG(ERROR) << "Cannot get Power Hal Extension Binder";
        power_hal_aidl_exist_ = false;
        return false;
    }

    power_hal_ext_aidl_ = IPowerExt::fromBinder(ext_power_binder);
    if (power_hal_ext_aidl_ == nullptr) {
        LOG(ERROR) << "Cannot get Power Hal Extension AIDL";
        power_hal_aidl_exist_ = false;
    }

    return true;
}

bool PowerHalService::isModeSupported(const std::string &type, const ThrottlingSeverity &t) {
    bool isSupported = false;
    if (!isPowerHalConnected()) {
        return false;
    }
    std::string power_hint = StringPrintf("THERMAL_%s_%s", type.c_str(), toString(t).c_str());
    lock_.lock();
    if (!power_hal_ext_aidl_->isModeSupported(power_hint, &isSupported).isOk()) {
        LOG(ERROR) << "Fail to check supported mode, Hint: " << power_hint;
        power_hal_aidl_exist_ = false;
        power_hal_ext_aidl_ = nullptr;
        power_hal_aidl_ = nullptr;
        lock_.unlock();
        return false;
    }
    lock_.unlock();
    return isSupported;
}

void PowerHalService::setMode(const std::string &type, const ThrottlingSeverity &t,
                              const bool &enable) {
    if (!isPowerHalConnected()) {
        return;
    }

    std::string power_hint = StringPrintf("THERMAL_%s_%s", type.c_str(), toString(t).c_str());
    LOG(INFO) << "Send Hint " << power_hint << " Enable: " << std::boolalpha << enable;
    lock_.lock();
    if (!power_hal_ext_aidl_->setMode(power_hint, enable).isOk()) {
        LOG(ERROR) << "Fail to set mode, Hint: " << power_hint;
        power_hal_aidl_exist_ = false;
        power_hal_ext_aidl_ = nullptr;
        power_hal_aidl_ = nullptr;
        lock_.unlock();
        return;
    }
    lock_.unlock();
}

/*
 * Populate the sensor_name_to_file_map_ map by walking through the file tree,
 * reading the type file and assigning the temp file path to the map.  If we do
 * not succeed, abort.
 */
ThermalHelper::ThermalHelper(const NotificationCallback &cb)
    : thermal_watcher_(new ThermalWatcher(
              std::bind(&ThermalHelper::thermalWatcherCallbackFunc, this, std::placeholders::_1))),
      cb_(cb) {
    const std::string config_path =
            "/vendor/etc/" +
            android::base::GetProperty(kConfigProperty.data(), kConfigDefaultFileName.data());
    cooling_device_info_map_ = ParseCoolingDevice(config_path);
    sensor_info_map_ = ParseSensorInfo(config_path);
    power_rail_info_map_ = ParsePowerRailInfo(config_path);
    auto tz_map = parseThermalPathMap(kSensorPrefix.data());
    auto cdev_map = parseThermalPathMap(kCoolingDevicePrefix.data());

    is_initialized_ = initializeSensorMap(tz_map) && initializeCoolingDevices(cdev_map);
    if (!is_initialized_) {
        LOG(FATAL) << "ThermalHAL could not be initialized properly.";
    }

    for (auto const &name_status_pair : sensor_info_map_) {
        sensor_status_map_[name_status_pair.first] = {
                .severity = ThrottlingSeverity::NONE,
                .prev_hot_severity = ThrottlingSeverity::NONE,
                .prev_cold_severity = ThrottlingSeverity::NONE,
                .prev_hint_severity = ThrottlingSeverity::NONE,
                .last_update_time = boot_clock::time_point::min(),
                .err_integral = 0.0,
                .prev_err = NAN,
        };

        bool invalid_binded_cdev = false;
        for (auto &binded_cdev_pair :
             name_status_pair.second.throttling_info->binded_cdev_info_map) {
            if (!cooling_device_info_map_.count(binded_cdev_pair.first)) {
                invalid_binded_cdev = true;
                LOG(ERROR) << "Could not find " << binded_cdev_pair.first
                           << " in cooling device info map";
            }

            for (const auto &cdev_weight : binded_cdev_pair.second.cdev_weight_for_pid) {
                if (!std::isnan(cdev_weight)) {
                    sensor_status_map_[name_status_pair.first]
                            .pid_request_map[binded_cdev_pair.first] = 0;
                    cdev_status_map_[binded_cdev_pair.first][name_status_pair.first] = 0;
                    break;
                }
            }

            for (const auto &limit_info : binded_cdev_pair.second.limit_info) {
                if (limit_info > 0) {
                    sensor_status_map_[name_status_pair.first]
                            .hard_limit_request_map[binded_cdev_pair.first] = 0;
                    cdev_status_map_[binded_cdev_pair.first][name_status_pair.first] = 0;
                }
            }
            const auto &cdev_info = cooling_device_info_map_.at(binded_cdev_pair.first);

            for (auto &cdev_ceiling : binded_cdev_pair.second.cdev_ceiling) {
                if (cdev_ceiling > cdev_info.max_state) {
                    if (cdev_ceiling != std::numeric_limits<int>::max()) {
                        LOG(ERROR) << "Sensor " << name_status_pair.first << "'s "
                                   << binded_cdev_pair.first << " cdev_ceiling:" << cdev_ceiling
                                   << " is higher than max state:" << cdev_info.max_state;
                    }
                    cdev_ceiling = cdev_info.max_state;
                }
            }

            if (power_rail_info_map_.count(binded_cdev_pair.second.power_rail) &&
                power_rail_info_map_.at(binded_cdev_pair.second.power_rail).power_sample_count &&
                power_files_.findEnergySourceToWatch()) {
                const auto &power_rail_info =
                        power_rail_info_map_.at(binded_cdev_pair.second.power_rail);
                if (!power_files_.registerPowerRailsToWatch(
                            name_status_pair.first, binded_cdev_pair.first, binded_cdev_pair.second,
                            cdev_info, power_rail_info)) {
                    invalid_binded_cdev = true;
                    LOG(ERROR) << "Could not find " << binded_cdev_pair.first
                               << "'s power energy source: " << binded_cdev_pair.second.power_rail;
                }
            }
        }

        if (invalid_binded_cdev) {
            name_status_pair.second.throttling_info->binded_cdev_info_map.clear();
            sensor_status_map_[name_status_pair.first].hard_limit_request_map.clear();
            sensor_status_map_[name_status_pair.first].pid_request_map.clear();
        }

        if (name_status_pair.second.virtual_sensor_info != nullptr &&
            name_status_pair.second.is_monitor) {
            if (sensor_info_map_.count(
                        name_status_pair.second.virtual_sensor_info->trigger_sensor)) {
                sensor_info_map_[name_status_pair.second.virtual_sensor_info->trigger_sensor]
                        .is_monitor = true;
            } else {
                LOG(FATAL) << name_status_pair.first << " does not have trigger sensor: "
                           << name_status_pair.second.virtual_sensor_info->trigger_sensor;
            }
        }
    }

    const bool thermal_throttling_disabled =
            android::base::GetBoolProperty(kThermalDisabledProperty.data(), false);

    if (thermal_throttling_disabled) {
        LOG(INFO) << kThermalDisabledProperty.data() << " is true";
        for (const auto &cdev_pair : cooling_device_info_map_) {
            if (cooling_devices_.writeCdevFile(cdev_pair.first, std::to_string(0))) {
                LOG(INFO) << "Successfully clear cdev " << cdev_pair.first << " to 0";
            }
        }
        return;
    }

    const bool thermal_genl_enabled =
            android::base::GetBoolProperty(kThermalGenlProperty.data(), false);

    std::set<std::string> monitored_sensors;
    initializeTrip(tz_map, &monitored_sensors, thermal_genl_enabled);

    if (thermal_genl_enabled) {
        thermal_watcher_->registerFilesToWatchNl(monitored_sensors);
    } else {
        thermal_watcher_->registerFilesToWatch(monitored_sensors);
    }

    // Need start watching after status map initialized
    is_initialized_ = thermal_watcher_->startWatchingDeviceFiles();
    if (!is_initialized_) {
        LOG(FATAL) << "ThermalHAL could not start watching thread properly.";
    }

    if (!connectToPowerHal()) {
        LOG(ERROR) << "Fail to connect to Power Hal";
    } else {
        updateSupportedPowerHints();
    }
}

bool getThermalZoneTypeById(int tz_id, std::string *type) {
    std::string tz_type;
    std::string path =
            android::base::StringPrintf("%s/%s%d/%s", kThermalSensorsRoot.data(),
                                        kSensorPrefix.data(), tz_id, kThermalNameFile.data());
    LOG(INFO) << "TZ Path: " << path;
    if (!::android::base::ReadFileToString(path, &tz_type)) {
        LOG(ERROR) << "Failed to read sensor: " << tz_type;
        return false;
    }

    // Strip the newline.
    *type = ::android::base::Trim(tz_type);
    LOG(INFO) << "TZ type: " << *type;
    return true;
}

bool ThermalHelper::readCoolingDevice(std::string_view cooling_device,
                                      CoolingDevice_2_0 *out) const {
    // Read the file.  If the file can't be read temp will be empty string.
    std::string data;

    if (!cooling_devices_.readThermalFile(cooling_device, &data)) {
        LOG(ERROR) << "readCoolingDevice: failed to read cooling_device: " << cooling_device;
        return false;
    }

    const CdevInfo &cdev_info = cooling_device_info_map_.at(cooling_device.data());
    const CoolingType &type = cdev_info.type;

    out->type = type;
    out->name = cooling_device.data();
    out->value = std::stoi(data);

    return true;
}

bool ThermalHelper::readTemperature(std::string_view sensor_name, Temperature_1_0 *out,
                                    bool is_virtual_sensor) const {
    // Read the file.  If the file can't be read temp will be empty string.
    std::string temp;

    if (!is_virtual_sensor) {
        if (!thermal_sensors_.readThermalFile(sensor_name, &temp)) {
            LOG(ERROR) << "readTemperature: sensor not found: " << sensor_name;
            return false;
        }

        if (temp.empty()) {
            LOG(ERROR) << "readTemperature: failed to read sensor: " << sensor_name;
            return false;
        }
    } else {
        if (!checkVirtualSensor(sensor_name.data(), &temp)) {
            LOG(ERROR) << "readTemperature: failed to read virtual sensor: " << sensor_name;
            return false;
        }
    }

    const SensorInfo &sensor_info = sensor_info_map_.at(sensor_name.data());
    TemperatureType_1_0 type =
        (static_cast<int>(sensor_info.type) > static_cast<int>(TemperatureType_1_0::SKIN))
            ? TemperatureType_1_0::UNKNOWN
            : static_cast<TemperatureType_1_0>(sensor_info.type);
    out->type = type;
    out->name = sensor_name.data();
    out->currentValue = std::stof(temp) * sensor_info.multiplier;
    out->throttlingThreshold =
        sensor_info.hot_thresholds[static_cast<size_t>(ThrottlingSeverity::SEVERE)];
    out->shutdownThreshold =
        sensor_info.hot_thresholds[static_cast<size_t>(ThrottlingSeverity::SHUTDOWN)];
    out->vrThrottlingThreshold = sensor_info.vr_threshold;

    return true;
}

bool ThermalHelper::readTemperature(
        std::string_view sensor_name, Temperature_2_0 *out,
        std::pair<ThrottlingSeverity, ThrottlingSeverity> *throtting_status,
        bool is_virtual_sensor) const {
    // Read the file.  If the file can't be read temp will be empty string.
    std::string temp;

    if (!is_virtual_sensor) {
        if (!thermal_sensors_.readThermalFile(sensor_name, &temp)) {
            LOG(ERROR) << "readTemperature: sensor not found: " << sensor_name;
            return false;
        }

        if (temp.empty()) {
            LOG(ERROR) << "readTemperature: failed to read sensor: " << sensor_name;
            return false;
        }
    } else {
        if (!checkVirtualSensor(sensor_name.data(), &temp)) {
            LOG(ERROR) << "readTemperature: failed to read virtual sensor: " << sensor_name;
            return false;
        }
    }

    const auto &sensor_info = sensor_info_map_.at(sensor_name.data());
    out->type = sensor_info.type;
    out->name = sensor_name.data();
    out->value = std::stof(temp) * sensor_info.multiplier;

    std::pair<ThrottlingSeverity, ThrottlingSeverity> status =
        std::make_pair(ThrottlingSeverity::NONE, ThrottlingSeverity::NONE);
    // Only update status if the thermal sensor is being monitored
    if (sensor_info.is_monitor) {
        ThrottlingSeverity prev_hot_severity, prev_cold_severity;
        {
            // reader lock, readTemperature will be called in Binder call and the watcher thread.
            std::shared_lock<std::shared_mutex> _lock(sensor_status_map_mutex_);
            prev_hot_severity = sensor_status_map_.at(sensor_name.data()).prev_hot_severity;
            prev_cold_severity = sensor_status_map_.at(sensor_name.data()).prev_cold_severity;
        }
        status = getSeverityFromThresholds(sensor_info.hot_thresholds, sensor_info.cold_thresholds,
                                           sensor_info.hot_hysteresis, sensor_info.cold_hysteresis,
                                           prev_hot_severity, prev_cold_severity, out->value);
    }
    if (throtting_status) {
        *throtting_status = status;
    }

    out->throttlingStatus = static_cast<size_t>(status.first) > static_cast<size_t>(status.second)
                                ? status.first
                                : status.second;

    return true;
}

bool ThermalHelper::readTemperatureThreshold(std::string_view sensor_name,
                                             TemperatureThreshold *out) const {
    // Read the file.  If the file can't be read temp will be empty string.
    std::string temp;
    std::string path;

    if (!sensor_info_map_.count(sensor_name.data())) {
        LOG(ERROR) << __func__ << ": sensor not found: " << sensor_name;
        return false;
    }

    const auto &sensor_info = sensor_info_map_.at(sensor_name.data());

    out->type = sensor_info.type;
    out->name = sensor_name.data();
    out->hotThrottlingThresholds = sensor_info.hot_thresholds;
    out->coldThrottlingThresholds = sensor_info.cold_thresholds;
    out->vrThrottlingThreshold = sensor_info.vr_threshold;
    return true;
}

// To find the next PID target state according to the current thermal severity
size_t ThermalHelper::getTargetStateOfPID(const SensorInfo &sensor_info,
                                          const SensorStatus &sensor_status) {
    size_t target_state = 0;

    for (const auto &severity : hidl_enum_range<ThrottlingSeverity>()) {
        size_t state = static_cast<size_t>(severity);
        if (std::isnan(sensor_info.throttling_info->s_power[state])) {
            continue;
        }
        target_state = state;
        if (severity > sensor_status.severity) {
            break;
        }
    }
    return target_state;
}

// Return the power budget which is computed by PID algorithm
float ThermalHelper::pidPowerCalculator(const Temperature_2_0 &temp, const SensorInfo &sensor_info,
                                        SensorStatus *sensor_status,
                                        std::chrono::milliseconds time_elapsed_ms,
                                        size_t target_state) {
    float p = 0, i = 0, d = 0;
    float power_budget = std::numeric_limits<float>::max();

    LOG(VERBOSE) << "PID target state=" << target_state;
    if (!target_state || (sensor_status->severity == ThrottlingSeverity::NONE)) {
        sensor_status->err_integral = 0;
        sensor_status->prev_err = NAN;
        return power_budget;
    }

    // Compute PID
    float err = sensor_info.hot_thresholds[target_state] - temp.value;
    p = err * (err < 0 ? sensor_info.throttling_info->k_po[target_state]
                       : sensor_info.throttling_info->k_pu[target_state]);
    i = sensor_status->err_integral * sensor_info.throttling_info->k_i[target_state];
    if (err < sensor_info.throttling_info->i_cutoff[target_state]) {
        float i_next = i + err * sensor_info.throttling_info->k_i[target_state];
        if (abs(i_next) < sensor_info.throttling_info->i_max[target_state]) {
            i = i_next;
            sensor_status->err_integral += err;
        }
    }

    if (!std::isnan(sensor_status->prev_err) &&
        time_elapsed_ms != std::chrono::milliseconds::zero()) {
        d = sensor_info.throttling_info->k_d[target_state] * (err - sensor_status->prev_err) /
            time_elapsed_ms.count();
    }

    sensor_status->prev_err = err;
    // Calculate power budget
    power_budget = sensor_info.throttling_info->s_power[target_state] + p + i + d;
    if (power_budget < sensor_info.throttling_info->min_alloc_power[target_state]) {
        power_budget = sensor_info.throttling_info->min_alloc_power[target_state];
    }
    if (power_budget > sensor_info.throttling_info->max_alloc_power[target_state]) {
        power_budget = sensor_info.throttling_info->max_alloc_power[target_state];
    }

    LOG(VERBOSE) << "power_budget=" << power_budget << " err=" << err
                 << " err_integral=" << sensor_status->err_integral
                 << " s_power=" << sensor_info.throttling_info->s_power[target_state]
                 << " time_elpased_ms=" << time_elapsed_ms.count() << " p=" << p << " i=" << i
                 << " d=" << d;

    return power_budget;
}

bool ThermalHelper::requestCdevByPower(std::string_view sensor_name, SensorStatus *sensor_status,
                                       const SensorInfo &sensor_info, float total_power_budget,
                                       size_t target_state) {
    float total_weight = 0, cdev_power_budget;
    size_t j;

    for (const auto &binded_cdev_info_pair : sensor_info.throttling_info->binded_cdev_info_map) {
        if (!std::isnan(binded_cdev_info_pair.second.cdev_weight_for_pid[target_state])) {
            total_weight += binded_cdev_info_pair.second.cdev_weight_for_pid[target_state];
        }
    }

    if (!total_weight) {
        LOG(ERROR) << "Sensor: " << sensor_name.data() << " total weight value is zero";
        return false;
    }

    // Map cdev state by power
    for (const auto &binded_cdev_info_pair : sensor_info.throttling_info->binded_cdev_info_map) {
        const auto cdev_weight = binded_cdev_info_pair.second.cdev_weight_for_pid[target_state];
        if (!std::isnan(cdev_weight)) {
            cdev_power_budget = total_power_budget * (cdev_weight / total_weight);

            const CdevInfo &cdev_info_pair =
                    cooling_device_info_map_.at(binded_cdev_info_pair.first);
            for (j = 0; j < cdev_info_pair.state2power.size() - 1; ++j) {
                if (cdev_power_budget > cdev_info_pair.state2power[j]) {
                    break;
                }
            }
            sensor_status->pid_request_map.at(binded_cdev_info_pair.first) = static_cast<int>(j);
            LOG(VERBOSE) << "Power allocator: Sensor " << sensor_name.data() << " allocate "
                         << cdev_power_budget << "mW to " << binded_cdev_info_pair.first
                         << "(cdev_weight=" << cdev_weight << ") update state to " << j;
        }
    }
    return true;
}

void ThermalHelper::requestCdevBySeverity(std::string_view sensor_name, SensorStatus *sensor_status,
                                          const SensorInfo &sensor_info) {
    for (auto const &binded_cdev_info_pair : sensor_info.throttling_info->binded_cdev_info_map) {
        sensor_status->hard_limit_request_map.at(binded_cdev_info_pair.first) =
                binded_cdev_info_pair.second
                        .limit_info[static_cast<size_t>(sensor_status->severity)];
        LOG(VERBOSE) << "Hard Limit: Sensor " << sensor_name.data() << " update cdev "
                     << binded_cdev_info_pair.first << " to "
                     << sensor_status->hard_limit_request_map.at(binded_cdev_info_pair.first);
    }
}

void ThermalHelper::computeCoolingDevicesRequest(
        std::string_view sensor_name, const SensorInfo &sensor_info,
        const SensorStatus &sensor_status, std::vector<std::string> *cooling_devices_to_update) {
    int release_step = 0;

    std::unique_lock<std::shared_mutex> _lock(cdev_status_map_mutex_);
    for (auto &cdev_request_pair : cdev_status_map_) {
        if (!cdev_request_pair.second.count(sensor_name.data())) {
            continue;
        }
        int pid_request = 0;
        int hard_limit_request = 0;
        const auto &binded_cdev_info =
                sensor_info.throttling_info->binded_cdev_info_map.at(cdev_request_pair.first);
        const auto cdev_ceiling =
                binded_cdev_info.cdev_ceiling[static_cast<size_t>(sensor_status.severity)];
        const auto cdev_floor =
                binded_cdev_info
                        .cdev_floor_with_power_link[static_cast<size_t>(sensor_status.severity)];
        release_step = 0;

        if (sensor_status.pid_request_map.count(cdev_request_pair.first)) {
            pid_request = sensor_status.pid_request_map.at(cdev_request_pair.first);
        }

        if (sensor_status.hard_limit_request_map.count(cdev_request_pair.first)) {
            hard_limit_request = sensor_status.hard_limit_request_map.at(cdev_request_pair.first);
        }

        release_step = power_files_.getReleaseStep(sensor_name, cdev_request_pair.first);
        LOG(VERBOSE) << "Sensor: " << sensor_name.data() << " binded cooling device "
                     << cdev_request_pair.first << "'s pid_request=" << pid_request
                     << " hard_limit_request=" << hard_limit_request
                     << " release_step=" << release_step
                     << " cdev_floor_with_power_link=" << cdev_floor
                     << " cdev_ceiling=" << cdev_ceiling;

        auto request_state = std::max(pid_request, hard_limit_request);
        if (release_step) {
            if (release_step >= request_state) {
                request_state = 0;
            } else {
                request_state = request_state - release_step;
            }
            // Only check the cdev_floor when release step is non zero
            if (request_state < cdev_floor) {
                request_state = cdev_floor;
            }
        }

        if (request_state > cdev_ceiling) {
            request_state = cdev_ceiling;
        }
        if (cdev_request_pair.second.at(sensor_name.data()) != request_state) {
            cdev_request_pair.second.at(sensor_name.data()) = request_state;
            cooling_devices_to_update->emplace_back(cdev_request_pair.first);
            LOG(INFO) << "Sensor: " << sensor_name.data() << " request " << cdev_request_pair.first
                      << " to " << request_state;
        }
    }
}

void ThermalHelper::updateCoolingDevices(const std::vector<std::string> &updated_cdev) {
    int max_state;

    for (const auto &target_cdev : updated_cdev) {
        max_state = 0;
        const CdevRequestStatus &cdev_status = cdev_status_map_.at(target_cdev);
        for (auto &sensor_request_pair : cdev_status) {
            if (sensor_request_pair.second > max_state) {
                max_state = sensor_request_pair.second;
            }
        }
        if (cooling_devices_.writeCdevFile(target_cdev, std::to_string(max_state))) {
            LOG(VERBOSE) << "Successfully update cdev " << target_cdev << " sysfs to " << max_state;
        }
    }
}

std::pair<ThrottlingSeverity, ThrottlingSeverity> ThermalHelper::getSeverityFromThresholds(
    const ThrottlingArray &hot_thresholds, const ThrottlingArray &cold_thresholds,
    const ThrottlingArray &hot_hysteresis, const ThrottlingArray &cold_hysteresis,
    ThrottlingSeverity prev_hot_severity, ThrottlingSeverity prev_cold_severity,
    float value) const {
    ThrottlingSeverity ret_hot = ThrottlingSeverity::NONE;
    ThrottlingSeverity ret_hot_hysteresis = ThrottlingSeverity::NONE;
    ThrottlingSeverity ret_cold = ThrottlingSeverity::NONE;
    ThrottlingSeverity ret_cold_hysteresis = ThrottlingSeverity::NONE;

    // Here we want to control the iteration from high to low, and hidl_enum_range doesn't support
    // a reverse iterator yet.
    for (size_t i = static_cast<size_t>(ThrottlingSeverity::SHUTDOWN);
         i > static_cast<size_t>(ThrottlingSeverity::NONE); --i) {
        if (!std::isnan(hot_thresholds[i]) && hot_thresholds[i] <= value &&
            ret_hot == ThrottlingSeverity::NONE) {
            ret_hot = static_cast<ThrottlingSeverity>(i);
        }
        if (!std::isnan(hot_thresholds[i]) && (hot_thresholds[i] - hot_hysteresis[i]) < value &&
            ret_hot_hysteresis == ThrottlingSeverity::NONE) {
            ret_hot_hysteresis = static_cast<ThrottlingSeverity>(i);
        }
        if (!std::isnan(cold_thresholds[i]) && cold_thresholds[i] >= value &&
            ret_cold == ThrottlingSeverity::NONE) {
            ret_cold = static_cast<ThrottlingSeverity>(i);
        }
        if (!std::isnan(cold_thresholds[i]) && (cold_thresholds[i] + cold_hysteresis[i]) > value &&
            ret_cold_hysteresis == ThrottlingSeverity::NONE) {
            ret_cold_hysteresis = static_cast<ThrottlingSeverity>(i);
        }
    }
    if (static_cast<size_t>(ret_hot) < static_cast<size_t>(prev_hot_severity)) {
        ret_hot = ret_hot_hysteresis;
    }
    if (static_cast<size_t>(ret_cold) < static_cast<size_t>(prev_cold_severity)) {
        ret_cold = ret_cold_hysteresis;
    }

    return std::make_pair(ret_hot, ret_cold);
}

bool ThermalHelper::initializeSensorMap(
        const std::unordered_map<std::string, std::string> &path_map) {
    for (const auto &sensor_info_pair : sensor_info_map_) {
        std::string_view sensor_name = sensor_info_pair.first;
        if (sensor_info_pair.second.virtual_sensor_info != nullptr) {
            continue;
        }
        if (!path_map.count(sensor_name.data())) {
            LOG(ERROR) << "Could not find " << sensor_name << " in sysfs";
            return false;
        }

        std::string path;
        if (sensor_info_pair.second.temp_path.empty()) {
            path = android::base::StringPrintf("%s/%s", path_map.at(sensor_name.data()).c_str(),
                                               kSensorTempSuffix.data());
        } else {
            path = sensor_info_pair.second.temp_path;
        }

        if (!thermal_sensors_.addThermalFile(sensor_name, path)) {
            LOG(ERROR) << "Could not add " << sensor_name << "to sensors map";
            return false;
        }
    }
    return true;
}

bool ThermalHelper::initializeCoolingDevices(
        const std::unordered_map<std::string, std::string> &path_map) {
    for (auto &cooling_device_info_pair : cooling_device_info_map_) {
        std::string cooling_device_name = cooling_device_info_pair.first;
        if (!path_map.count(cooling_device_name)) {
            LOG(ERROR) << "Could not find " << cooling_device_name << " in sysfs";
            continue;
        }
        // Add cooling device path for thermalHAL to get current state
        std::string_view path = path_map.at(cooling_device_name);
        std::string read_path;
        if (!cooling_device_info_pair.second.read_path.empty()) {
            read_path = cooling_device_info_pair.second.read_path.data();
        } else {
            read_path = android::base::StringPrintf("%s/%s", path.data(),
                                                    kCoolingDeviceCurStateSuffix.data());
        }
        if (!cooling_devices_.addThermalFile(cooling_device_name, read_path)) {
            LOG(ERROR) << "Could not add " << cooling_device_name
                       << " read path to cooling device map";
            continue;
        }

        std::string state2power_path = android::base::StringPrintf(
                "%s/%s", path.data(), kCoolingDeviceState2powerSuffix.data());
        std::string state2power_str;
        if (android::base::ReadFileToString(state2power_path, &state2power_str)) {
            LOG(INFO) << "Cooling device " << cooling_device_info_pair.first
                      << " use state2power read from sysfs";
            cooling_device_info_pair.second.state2power.clear();

            std::stringstream power(state2power_str);
            unsigned int power_number;
            int i = 0;
            while (power >> power_number) {
                cooling_device_info_pair.second.state2power.push_back(
                        static_cast<float>(power_number));
                LOG(INFO) << "Cooling device " << cooling_device_info_pair.first << " state:" << i
                          << " power: " << power_number;
                i++;
            }
        }

        // Get max cooling device request state
        std::string max_state;
        std::string max_state_path = android::base::StringPrintf(
                "%s/%s", path.data(), kCoolingDeviceMaxStateSuffix.data());
        if (!android::base::ReadFileToString(max_state_path, &max_state)) {
            LOG(ERROR) << cooling_device_info_pair.first
                       << " could not open max state file:" << max_state_path;
            cooling_device_info_pair.second.max_state = std::numeric_limits<int>::max();
        } else {
            cooling_device_info_pair.second.max_state = std::stoi(android::base::Trim(max_state));
            LOG(INFO) << "Cooling device " << cooling_device_info_pair.first
                      << " max state: " << cooling_device_info_pair.second.max_state
                      << " state2power number: "
                      << cooling_device_info_pair.second.state2power.size();
            if (cooling_device_info_pair.second.state2power.size() > 0 &&
                cooling_device_info_pair.second.state2power.size() !=
                        (size_t)cooling_device_info_pair.second.max_state + 1) {
                LOG(ERROR) << "Invalid state2power number: "
                           << cooling_device_info_pair.second.state2power.size()
                           << ", number should be " << cooling_device_info_pair.second.max_state + 1
                           << " (max_state + 1)";
            }
        }

        // Add cooling device path for thermalHAL to request state
        cooling_device_name =
                android::base::StringPrintf("%s_%s", cooling_device_name.c_str(), "w");
        std::string write_path;
        if (!cooling_device_info_pair.second.write_path.empty()) {
            write_path = cooling_device_info_pair.second.write_path.data();
        } else {
            write_path = android::base::StringPrintf("%s/%s", path.data(),
                                                     kCoolingDeviceCurStateSuffix.data());
        }

        if (!cooling_devices_.addThermalFile(cooling_device_name, write_path)) {
            LOG(ERROR) << "Could not add " << cooling_device_name
                       << " write path to cooling device map";
            continue;
        }
    }

    if (cooling_device_info_map_.size() * 2 != cooling_devices_.getNumThermalFiles()) {
        LOG(ERROR) << "Some cooling device can not be initialized";
    }
    return true;
}

void ThermalHelper::setMinTimeout(SensorInfo *sensor_info) {
    sensor_info->polling_delay = kMinPollIntervalMs;
    sensor_info->passive_delay = kMinPollIntervalMs;
}

void ThermalHelper::initializeTrip(const std::unordered_map<std::string, std::string> &path_map,
                                   std::set<std::string> *monitored_sensors,
                                   bool thermal_genl_enabled) {
    for (auto &sensor_info : sensor_info_map_) {
        if (!sensor_info.second.is_monitor || (sensor_info.second.virtual_sensor_info != nullptr)) {
            continue;
        }

        bool trip_update = false;
        std::string_view sensor_name = sensor_info.first;
        std::string_view tz_path = path_map.at(sensor_name.data());
        std::string tz_policy;
        std::string path =
                android::base::StringPrintf("%s/%s", (tz_path.data()), kSensorPolicyFile.data());

        if (thermal_genl_enabled) {
            trip_update = true;
        } else {
            // Check if thermal zone support uevent notify
            if (!android::base::ReadFileToString(path, &tz_policy)) {
                LOG(ERROR) << sensor_name << " could not open tz policy file:" << path;
            } else {
                tz_policy = android::base::Trim(tz_policy);
                if (tz_policy != kUserSpaceSuffix) {
                    LOG(ERROR) << sensor_name << " does not support uevent notify";
                } else {
                    trip_update = true;
                }
            }
        }
        if (trip_update) {
            // Update thermal zone trip point
            for (size_t i = 0; i < kThrottlingSeverityCount; ++i) {
                if (!std::isnan(sensor_info.second.hot_thresholds[i]) &&
                    !std::isnan(sensor_info.second.hot_hysteresis[i])) {
                    // Update trip_point_0_temp threshold
                    std::string threshold = std::to_string(static_cast<int>(
                            sensor_info.second.hot_thresholds[i] / sensor_info.second.multiplier));
                    path = android::base::StringPrintf("%s/%s", (tz_path.data()),
                                                       kSensorTripPointTempZeroFile.data());
                    if (!android::base::WriteStringToFile(threshold, path)) {
                        LOG(ERROR) << "fail to update " << sensor_name << " trip point: " << path
                                   << " to " << threshold;
                        trip_update = false;
                        break;
                    }
                    // Update trip_point_0_hyst threshold
                    threshold = std::to_string(static_cast<int>(
                            sensor_info.second.hot_hysteresis[i] / sensor_info.second.multiplier));
                    path = android::base::StringPrintf("%s/%s", (tz_path.data()),
                                                       kSensorTripPointHystZeroFile.data());
                    if (!android::base::WriteStringToFile(threshold, path)) {
                        LOG(ERROR) << "fail to update " << sensor_name << "trip hyst" << threshold
                                   << path;
                        trip_update = false;
                        break;
                    }
                    break;
                } else if (i == kThrottlingSeverityCount - 1) {
                    LOG(ERROR) << sensor_name << ":all thresholds are NAN";
                    trip_update = false;
                    break;
                }
            }
            monitored_sensors->insert(sensor_info.first);
        }

        if (!trip_update) {
            LOG(INFO) << "config Sensor: " << sensor_info.first
                      << " to default polling interval: " << kMinPollIntervalMs.count();
            setMinTimeout(&sensor_info.second);
        }
    }
}

bool ThermalHelper::fillTemperatures(hidl_vec<Temperature_1_0> *temperatures) const {
    temperatures->resize(sensor_info_map_.size());
    int current_index = 0;
    for (const auto &name_info_pair : sensor_info_map_) {
        Temperature_1_0 temp;

        if (readTemperature(name_info_pair.first, &temp,
                            name_info_pair.second.virtual_sensor_info != nullptr)) {
            (*temperatures)[current_index] = temp;
        } else {
            LOG(ERROR) << __func__
                       << ": error reading temperature for sensor: " << name_info_pair.first;
            return false;
        }
        ++current_index;
    }
    return current_index > 0;
}

bool ThermalHelper::fillCurrentTemperatures(bool filterType, bool filterCallback,
                                            TemperatureType_2_0 type,
                                            hidl_vec<Temperature_2_0> *temperatures) const {
    std::vector<Temperature_2_0> ret;
    for (const auto &name_info_pair : sensor_info_map_) {
        Temperature_2_0 temp;
        if (filterType && name_info_pair.second.type != type) {
            continue;
        }
        if (filterCallback && !name_info_pair.second.send_cb) {
            continue;
        }
        if (readTemperature(name_info_pair.first, &temp, nullptr,
                            name_info_pair.second.virtual_sensor_info != nullptr)) {
            ret.emplace_back(std::move(temp));
        } else {
            LOG(ERROR) << __func__
                       << ": error reading temperature for sensor: " << name_info_pair.first;
        }
    }
    *temperatures = ret;
    return ret.size() > 0;
}

bool ThermalHelper::fillTemperatureThresholds(bool filterType, TemperatureType_2_0 type,
                                              hidl_vec<TemperatureThreshold> *thresholds) const {
    std::vector<TemperatureThreshold> ret;
    for (const auto &name_info_pair : sensor_info_map_) {
        TemperatureThreshold temp;
        if (filterType && name_info_pair.second.type != type) {
            continue;
        }
        if (readTemperatureThreshold(name_info_pair.first, &temp)) {
            ret.emplace_back(std::move(temp));
        } else {
            LOG(ERROR) << __func__ << ": error reading temperature threshold for sensor: "
                       << name_info_pair.first;
            return false;
        }
    }
    *thresholds = ret;
    return ret.size() > 0;
}

bool ThermalHelper::fillCurrentCoolingDevices(bool filterType, CoolingType type,
                                              hidl_vec<CoolingDevice_2_0> *cooling_devices) const {
    std::vector<CoolingDevice_2_0> ret;
    for (const auto &name_info_pair : cooling_device_info_map_) {
        CoolingDevice_2_0 value;
        if (filterType && name_info_pair.second.type != type) {
            continue;
        }
        if (readCoolingDevice(name_info_pair.first, &value)) {
            ret.emplace_back(std::move(value));
        } else {
            LOG(ERROR) << __func__ << ": error reading cooling device: " << name_info_pair.first;
            return false;
        }
    }
    *cooling_devices = ret;
    return ret.size() > 0;
}

bool ThermalHelper::fillCpuUsages(hidl_vec<CpuUsage> *cpu_usages) const {
    cpu_usages->resize(kMaxCpus);
    for (int i = 0; i < kMaxCpus; i++) {
        (*cpu_usages)[i].name = StringPrintf("cpu%d", i);
        (*cpu_usages)[i].active = 0;
        (*cpu_usages)[i].total = 0;
        (*cpu_usages)[i].isOnline = false;
    }
    parseCpuUsagesFileAndAssignUsages(cpu_usages);
    return true;
}

bool ThermalHelper::checkVirtualSensor(std::string_view sensor_name, std::string *temp) const {
    float temp_val = 0.0;

    const auto &sensor_info = sensor_info_map_.at(sensor_name.data());
    float offset = sensor_info.virtual_sensor_info->offset;
    for (size_t i = 0; i < sensor_info.virtual_sensor_info->linked_sensors.size(); i++) {
        std::string data;
        const auto &linked_sensor_info =
                sensor_info_map_.at(sensor_info.virtual_sensor_info->linked_sensors[i].data());
        if (linked_sensor_info.virtual_sensor_info == nullptr) {
            if (!thermal_sensors_.readThermalFile(
                        sensor_info.virtual_sensor_info->linked_sensors[i], &data)) {
                continue;
            }
        } else if (!checkVirtualSensor(sensor_info.virtual_sensor_info->linked_sensors[i], &data)) {
            return false;
        }

        LOG(VERBOSE) << sensor_name.data() << "'s linked sensor "
                     << sensor_info.virtual_sensor_info->linked_sensors[i] << ": temp = " << data;
        data = ::android::base::Trim(data);
        float sensor_reading = std::stof(data);
        if (std::isnan(sensor_info.virtual_sensor_info->coefficients[i])) {
            return false;
        }
        float coefficient = sensor_info.virtual_sensor_info->coefficients[i];
        switch (sensor_info.virtual_sensor_info->formula) {
            case FormulaOption::COUNT_THRESHOLD:
                if ((coefficient < 0 && sensor_reading < -coefficient) ||
                    (coefficient >= 0 && sensor_reading >= coefficient))
                    temp_val += 1;
                break;
            case FormulaOption::WEIGHTED_AVG:
                temp_val += sensor_reading * coefficient;
                break;
            case FormulaOption::MAXIMUM:
                if (i == 0)
                    temp_val = std::numeric_limits<float>::lowest();
                if (sensor_reading * coefficient > temp_val)
                    temp_val = sensor_reading * coefficient;
                break;
            case FormulaOption::MINIMUM:
                if (i == 0)
                    temp_val = std::numeric_limits<float>::max();
                if (sensor_reading * coefficient < temp_val)
                    temp_val = sensor_reading * coefficient;
                break;
            default:
                break;
        }
    }
    *temp = std::to_string(temp_val + offset);
    return true;
}

// This is called in the different thread context and will update sensor_status
// uevent_sensors is the set of sensors which trigger uevent from thermal core driver.
std::chrono::milliseconds ThermalHelper::thermalWatcherCallbackFunc(
        const std::set<std::string> &uevent_sensors) {
    std::vector<Temperature_2_0> temps;
    std::vector<std::string> cooling_devices_to_update;
    std::set<std::string> updated_power_rails;
    boot_clock::time_point now = boot_clock::now();
    auto min_sleep_ms = std::chrono::milliseconds::max();

    for (auto &name_status_pair : sensor_status_map_) {
        bool force_update = false;
        bool severity_changed = false;
        Temperature_2_0 temp;
        TemperatureThreshold threshold;
        SensorStatus &sensor_status = name_status_pair.second;
        const SensorInfo &sensor_info = sensor_info_map_.at(name_status_pair.first);

        // Only handle the sensors in allow list
        if (!sensor_info.is_monitor) {
            continue;
        }

        std::chrono::milliseconds time_elapsed_ms = std::chrono::milliseconds::zero();
        auto sleep_ms = (sensor_status.severity != ThrottlingSeverity::NONE)
                                ? sensor_info.passive_delay
                                : sensor_info.polling_delay;
        // Check if the sensor need to be updated
        if (sensor_status.last_update_time == boot_clock::time_point::min()) {
            force_update = true;
            LOG(VERBOSE) << "Force update " << name_status_pair.first
                         << "'s temperature after booting";
        } else {
            time_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - sensor_status.last_update_time);

            if (time_elapsed_ms > sleep_ms) {
                // Update the sensor because sleep timeout
                force_update = true;
            } else if (uevent_sensors.size() &&
                       uevent_sensors.find((sensor_info.virtual_sensor_info != nullptr)
                                                   ? sensor_info.virtual_sensor_info->trigger_sensor
                                                   : name_status_pair.first) !=
                               uevent_sensors.end()) {
                // Update the sensor from uevent
                force_update = true;
            } else if (sensor_info.virtual_sensor_info != nullptr) {
                // Update the virtual sensor if it's trigger sensor over the threshold
                const auto trigger_sensor_status =
                        sensor_status_map_.at(sensor_info.virtual_sensor_info->trigger_sensor);
                if (trigger_sensor_status.severity != ThrottlingSeverity::NONE) {
                    force_update = true;
                }
            }
        }

        LOG(VERBOSE) << "sensor " << name_status_pair.first
                     << ": time_elpased=" << time_elapsed_ms.count()
                     << ", sleep_ms=" << sleep_ms.count() << ", force_update = " << force_update;

        if (!force_update) {
            auto timeout_remaining = sleep_ms - time_elapsed_ms;
            if (min_sleep_ms > timeout_remaining) {
                min_sleep_ms = timeout_remaining;
            }
            LOG(VERBOSE) << "sensor " << name_status_pair.first
                         << ": timeout_remaining=" << timeout_remaining.count();
            continue;
        }

        std::pair<ThrottlingSeverity, ThrottlingSeverity> throtting_status;
        if (!readTemperature(name_status_pair.first, &temp, &throtting_status,
                             (sensor_info.virtual_sensor_info != nullptr))) {
            LOG(ERROR) << __func__
                       << ": error reading temperature for sensor: " << name_status_pair.first;
            continue;
        }
        if (!readTemperatureThreshold(name_status_pair.first, &threshold)) {
            LOG(ERROR) << __func__ << ": error reading temperature threshold for sensor: "
                       << name_status_pair.first;
            continue;
        }

        {
            // writer lock
            std::unique_lock<std::shared_mutex> _lock(sensor_status_map_mutex_);
            if (throtting_status.first != sensor_status.prev_hot_severity) {
                sensor_status.prev_hot_severity = throtting_status.first;
            }
            if (throtting_status.second != sensor_status.prev_cold_severity) {
                sensor_status.prev_cold_severity = throtting_status.second;
            }
            if (temp.throttlingStatus != sensor_status.severity) {
                temps.push_back(temp);
                severity_changed = true;
                sensor_status.severity = temp.throttlingStatus;
                sleep_ms = (sensor_status.severity != ThrottlingSeverity::NONE)
                                   ? sensor_info.passive_delay
                                   : sensor_info.polling_delay;
            }
        }

        if (sensor_status.severity != ThrottlingSeverity::NONE) {
            LOG(INFO) << temp.name << ": " << temp.value << " degC";
        } else {
            LOG(VERBOSE) << temp.name << ": " << temp.value << " degC";
        }

        // Start PID computation
        if (sensor_status.pid_request_map.size()) {
            size_t target_state = getTargetStateOfPID(sensor_info, sensor_status);
            float power_budget = pidPowerCalculator(temp, sensor_info, &sensor_status,
                                                    time_elapsed_ms, target_state);
            if (!requestCdevByPower(name_status_pair.first, &sensor_status, sensor_info,
                                    power_budget, target_state)) {
                LOG(ERROR) << "Sensor " << temp.name << " PID request cdev failed";
            }
        }

        if (sensor_status.hard_limit_request_map.size()) {
            // Start hard limit computation
            requestCdevBySeverity(name_status_pair.first, &sensor_status, sensor_info);
        }

        // Aggregate cooling device request
        if (sensor_status.pid_request_map.size() || sensor_status.hard_limit_request_map.size()) {
            if (sensor_status.severity == ThrottlingSeverity::NONE) {
                power_files_.setPowerDataToDefault(name_status_pair.first);
            } else {
                for (const auto &binded_cdev_info_pair :
                     sensor_info.throttling_info->binded_cdev_info_map) {
                    if (binded_cdev_info_pair.second.power_rail != "") {
                        const auto &power_rail_info =
                                power_rail_info_map_.at(binded_cdev_info_pair.second.power_rail);

                        if (power_files_.throttlingReleaseUpdate(
                                    name_status_pair.first, binded_cdev_info_pair.first,
                                    sensor_status.severity, time_elapsed_ms,
                                    binded_cdev_info_pair.second, power_rail_info,
                                    !updated_power_rails.count(
                                            binded_cdev_info_pair.second.power_rail),
                                    severity_changed)) {
                            updated_power_rails.insert(binded_cdev_info_pair.second.power_rail);
                        }
                    }
                }
            }
            computeCoolingDevicesRequest(name_status_pair.first, sensor_info, sensor_status,
                                         &cooling_devices_to_update);
        }

        if (min_sleep_ms > sleep_ms) {
            min_sleep_ms = sleep_ms;
        }
        LOG(VERBOSE) << "Sensor " << name_status_pair.first << ": sleep_ms=" << sleep_ms.count()
                     << ", min_sleep_ms voting result=" << min_sleep_ms.count();
        sensor_status.last_update_time = now;
    }

    if (!cooling_devices_to_update.empty()) {
        updateCoolingDevices(cooling_devices_to_update);
    }

    if (!temps.empty()) {
        for (const auto &t : temps) {
            if (sensor_info_map_.at(t.name).send_cb && cb_) {
                cb_(t);
            }

            if (sensor_info_map_.at(t.name).send_powerhint && isAidlPowerHalExist()) {
                sendPowerExtHint(t);
            }
        }
    }

    power_files_.clearEnergyInfoMap();
    return min_sleep_ms < kMinPollIntervalMs ? kMinPollIntervalMs : min_sleep_ms;
}

bool ThermalHelper::connectToPowerHal() {
    return power_hal_service_.connect();
}

void ThermalHelper::updateSupportedPowerHints() {
    for (auto const &name_status_pair : sensor_info_map_) {
        if (!(name_status_pair.second.send_powerhint)) {
            continue;
        }
        ThrottlingSeverity current_severity = ThrottlingSeverity::NONE;
        for (const auto &severity : hidl_enum_range<ThrottlingSeverity>()) {
            if (severity == ThrottlingSeverity::NONE) {
                supported_powerhint_map_[name_status_pair.first][ThrottlingSeverity::NONE] =
                        ThrottlingSeverity::NONE;
                continue;
            }

            bool isSupported = false;
            ndk::ScopedAStatus isSupportedResult;

            if (power_hal_service_.isPowerHalExtConnected()) {
                isSupported = power_hal_service_.isModeSupported(name_status_pair.first, severity);
            }
            if (isSupported)
                current_severity = severity;
            supported_powerhint_map_[name_status_pair.first][severity] = current_severity;
        }
    }
}

void ThermalHelper::sendPowerExtHint(const Temperature_2_0 &t) {
    std::lock_guard<std::shared_mutex> lock(sensor_status_map_mutex_);

    ThrottlingSeverity prev_hint_severity;
    prev_hint_severity = sensor_status_map_.at(t.name).prev_hint_severity;
    ThrottlingSeverity current_hint_severity = supported_powerhint_map_[t.name][t.throttlingStatus];

    if (prev_hint_severity == current_hint_severity)
        return;

    if (prev_hint_severity != ThrottlingSeverity::NONE) {
        power_hal_service_.setMode(t.name, prev_hint_severity, false);
    }

    if (current_hint_severity != ThrottlingSeverity::NONE) {
        power_hal_service_.setMode(t.name, current_hint_severity, true);
    }

    sensor_status_map_[t.name].prev_hint_severity = current_hint_severity;
}
}  // namespace implementation
}  // namespace V2_0
}  // namespace thermal
}  // namespace hardware
}  // namespace android
