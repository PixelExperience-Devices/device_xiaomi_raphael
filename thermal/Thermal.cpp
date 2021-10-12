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

#include <cerrno>
#include <mutex>
#include <string>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <hidl/HidlTransportSupport.h>

#include "Thermal.h"
#include "thermal-helper.h"

namespace android {
namespace hardware {
namespace thermal {
namespace V2_0 {
namespace implementation {

namespace {

using ::android::hardware::interfacesEqual;
using ::android::hardware::thermal::V1_0::ThermalStatus;
using ::android::hardware::thermal::V1_0::ThermalStatusCode;

template <typename T, typename U>
Return<void> setFailureAndCallback(T _hidl_cb, hidl_vec<U> data, std::string_view debug_msg) {
    ThermalStatus status;
    status.code = ThermalStatusCode::FAILURE;
    status.debugMessage = debug_msg.data();
    _hidl_cb(status, data);
    return Void();
}

template <typename T, typename U>
Return<void> setInitFailureAndCallback(T _hidl_cb, hidl_vec<U> data) {
    return setFailureAndCallback(_hidl_cb, data, "Failure initializing thermal HAL");
}

}  // namespace

// On init we will spawn a thread which will continually watch for
// throttling.  When throttling is seen, if we have a callback registered
// the thread will call notifyThrottling() else it will log the dropped
// throttling event and do nothing.  The thread is only killed when
// Thermal() is killed.
Thermal::Thermal()
    : thermal_helper_(
          std::bind(&Thermal::sendThermalChangedCallback, this, std::placeholders::_1)) {}

// Methods from ::android::hardware::thermal::V1_0::IThermal.
Return<void> Thermal::getTemperatures(getTemperatures_cb _hidl_cb) {
    ThermalStatus status;
    status.code = ThermalStatusCode::SUCCESS;
    hidl_vec<Temperature_1_0> temperatures;

    if (!thermal_helper_.isInitializedOk()) {
        LOG(ERROR) << "ThermalHAL not initialized properly.";
        return setInitFailureAndCallback(_hidl_cb, temperatures);
    }

    if (!thermal_helper_.fillTemperatures(&temperatures)) {
        return setFailureAndCallback(_hidl_cb, temperatures, "Failed to read thermal sensors.");
    }

    _hidl_cb(status, temperatures);
    return Void();
}

Return<void> Thermal::getCpuUsages(getCpuUsages_cb _hidl_cb) {
    ThermalStatus status;
    status.code = ThermalStatusCode::SUCCESS;
    hidl_vec<CpuUsage> cpu_usages;

    if (!thermal_helper_.isInitializedOk()) {
        return setInitFailureAndCallback(_hidl_cb, cpu_usages);
    }

    if (!thermal_helper_.fillCpuUsages(&cpu_usages)) {
        return setFailureAndCallback(_hidl_cb, cpu_usages, "Failed to get CPU usages.");
    }

    _hidl_cb(status, cpu_usages);
    return Void();
}

Return<void> Thermal::getCoolingDevices(getCoolingDevices_cb _hidl_cb) {
    ThermalStatus status;
    status.code = ThermalStatusCode::SUCCESS;
    hidl_vec<CoolingDevice_1_0> cooling_devices;

    if (!thermal_helper_.isInitializedOk()) {
        return setInitFailureAndCallback(_hidl_cb, cooling_devices);
    }
    _hidl_cb(status, cooling_devices);
    return Void();
}

Return<void> Thermal::getCurrentTemperatures(bool filterType, TemperatureType_2_0 type,
                                             getCurrentTemperatures_cb _hidl_cb) {
    ThermalStatus status;
    status.code = ThermalStatusCode::SUCCESS;
    hidl_vec<Temperature_2_0> temperatures;

    if (!thermal_helper_.isInitializedOk()) {
        LOG(ERROR) << "ThermalHAL not initialized properly.";
        return setInitFailureAndCallback(_hidl_cb, temperatures);
    }

    if (!thermal_helper_.fillCurrentTemperatures(filterType, type, &temperatures)) {
        return setFailureAndCallback(_hidl_cb, temperatures, "Failed to read thermal sensors.");
    }

    _hidl_cb(status, temperatures);
    return Void();
}

Return<void> Thermal::getTemperatureThresholds(bool filterType, TemperatureType_2_0 type,
                                               getTemperatureThresholds_cb _hidl_cb) {
    ThermalStatus status;
    status.code = ThermalStatusCode::SUCCESS;
    hidl_vec<TemperatureThreshold> temperatures;

    if (!thermal_helper_.isInitializedOk()) {
        LOG(ERROR) << "ThermalHAL not initialized properly.";
        return setInitFailureAndCallback(_hidl_cb, temperatures);
    }

    if (!thermal_helper_.fillTemperatureThresholds(filterType, type, &temperatures)) {
        return setFailureAndCallback(_hidl_cb, temperatures, "Failed to read thermal sensors.");
    }

    _hidl_cb(status, temperatures);
    return Void();
}

Return<void> Thermal::getCurrentCoolingDevices(bool filterType, CoolingType type,
                                               getCurrentCoolingDevices_cb _hidl_cb) {
    ThermalStatus status;
    status.code = ThermalStatusCode::SUCCESS;
    hidl_vec<CoolingDevice_2_0> cooling_devices;

    if (!thermal_helper_.isInitializedOk()) {
        LOG(ERROR) << "ThermalHAL not initialized properly.";
        return setInitFailureAndCallback(_hidl_cb, cooling_devices);
    }

    if (!thermal_helper_.fillCurrentCoolingDevices(filterType, type, &cooling_devices)) {
        return setFailureAndCallback(_hidl_cb, cooling_devices, "Failed to read thermal sensors.");
    }

    _hidl_cb(status, cooling_devices);
    return Void();
}

Return<void> Thermal::registerThermalChangedCallback(const sp<IThermalChangedCallback> &callback,
                                                     bool filterType, TemperatureType_2_0 type,
                                                     registerThermalChangedCallback_cb _hidl_cb) {
    ThermalStatus status;
    if (callback == nullptr) {
        status.code = ThermalStatusCode::FAILURE;
        status.debugMessage = "Invalid nullptr callback";
        LOG(ERROR) << status.debugMessage;
        _hidl_cb(status);
        return Void();
    } else {
        status.code = ThermalStatusCode::SUCCESS;
    }
    std::lock_guard<std::mutex> _lock(thermal_callback_mutex_);
    if (std::any_of(callbacks_.begin(), callbacks_.end(), [&](const CallbackSetting &c) {
            return interfacesEqual(c.callback, callback);
        })) {
        status.code = ThermalStatusCode::FAILURE;
        status.debugMessage = "Same callback registered already";
        LOG(ERROR) << status.debugMessage;
    } else {
        callbacks_.emplace_back(callback, filterType, type);
        LOG(INFO) << "a callback has been registered to ThermalHAL, isFilter: " << filterType
                  << " Type: " << android::hardware::thermal::V2_0::toString(type);
    }
    _hidl_cb(status);
    return Void();
}

Return<void> Thermal::unregisterThermalChangedCallback(
    const sp<IThermalChangedCallback> &callback, unregisterThermalChangedCallback_cb _hidl_cb) {
    ThermalStatus status;
    if (callback == nullptr) {
        status.code = ThermalStatusCode::FAILURE;
        status.debugMessage = "Invalid nullptr callback";
        LOG(ERROR) << status.debugMessage;
        _hidl_cb(status);
        return Void();
    } else {
        status.code = ThermalStatusCode::SUCCESS;
    }
    bool removed = false;
    std::lock_guard<std::mutex> _lock(thermal_callback_mutex_);
    callbacks_.erase(
        std::remove_if(callbacks_.begin(), callbacks_.end(),
                       [&](const CallbackSetting &c) {
                           if (interfacesEqual(c.callback, callback)) {
                               LOG(INFO)
                                   << "a callback has been unregistered to ThermalHAL, isFilter: "
                                   << c.is_filter_type << " Type: "
                                   << android::hardware::thermal::V2_0::toString(c.type);
                               removed = true;
                               return true;
                           }
                           return false;
                       }),
        callbacks_.end());
    if (!removed) {
        status.code = ThermalStatusCode::FAILURE;
        status.debugMessage = "The callback was not registered before";
        LOG(ERROR) << status.debugMessage;
    }
    _hidl_cb(status);
    return Void();
}

void Thermal::sendThermalChangedCallback(const Temperature_2_0 &t) {
    std::lock_guard<std::mutex> _lock(thermal_callback_mutex_);

    LOG(VERBOSE) << "Sending notification: "
                 << " Type: " << android::hardware::thermal::V2_0::toString(t.type)
                 << " Name: " << t.name << " CurrentValue: " << t.value << " ThrottlingStatus: "
                 << android::hardware::thermal::V2_0::toString(t.throttlingStatus);

    callbacks_.erase(
            std::remove_if(callbacks_.begin(), callbacks_.end(),
                           [&](const CallbackSetting &c) {
                               if (!c.is_filter_type || t.type == c.type) {
                                   Return<void> ret = c.callback->notifyThrottling(t);
                                   return !ret.isOk();
                               }
                               LOG(ERROR)
                                   << "a Thermal callback is dead, removed from callback list.";
                               return false;
                           }),
            callbacks_.end());
}

void Thermal::dumpVirtualSensorInfo(std::ostringstream *dump_buf) {
    *dump_buf << "VirtualSensorInfo:" << std::endl;
    const auto &map = thermal_helper_.GetSensorInfoMap();
    for (const auto &sensor_info_pair : map) {
        if (sensor_info_pair.second.virtual_sensor_info != nullptr) {
            *dump_buf << " Name: " << sensor_info_pair.first << std::endl;
            *dump_buf << "  LinkedSensorName: [";
            for (size_t i = 0;
                 i < sensor_info_pair.second.virtual_sensor_info->linked_sensors.size(); i++) {
                *dump_buf << sensor_info_pair.second.virtual_sensor_info->linked_sensors[i] << " ";
            }
            *dump_buf << "]" << std::endl;
            *dump_buf << "  LinkedSensorCoefficient: [";
            for (size_t i = 0; i < sensor_info_pair.second.virtual_sensor_info->coefficients.size();
                 i++) {
                *dump_buf << sensor_info_pair.second.virtual_sensor_info->coefficients[i] << " ";
            }
            *dump_buf << "]" << std::endl;
            *dump_buf << "  Offset: " << sensor_info_pair.second.virtual_sensor_info->offset
                      << std::endl;
            *dump_buf << "  Trigger Sensor: "
                      << (sensor_info_pair.second.virtual_sensor_info->trigger_sensor.empty()
                                  ? "N/A"
                                  : sensor_info_pair.second.virtual_sensor_info->trigger_sensor)
                      << std::endl;
            *dump_buf << "  Formula: ";
            switch (sensor_info_pair.second.virtual_sensor_info->formula) {
                case FormulaOption::COUNT_THRESHOLD:
                    *dump_buf << "COUNT_THRESHOLD";
                    break;
                case FormulaOption::WEIGHTED_AVG:
                    *dump_buf << "WEIGHTED_AVG";
                    break;
                case FormulaOption::MAXIMUM:
                    *dump_buf << "MAXIMUM";
                    break;
                case FormulaOption::MINIMUM:
                    *dump_buf << "MINIMUM";
                    break;
                default:
                    *dump_buf << "NONE";
                    break;
            }

            *dump_buf << std::endl;
        }
    }
}

void Thermal::dumpThrottlingInfo(std::ostringstream *dump_buf) {
    *dump_buf << "Throttling Info:" << std::endl;
    const auto &map = thermal_helper_.GetSensorInfoMap();
    for (const auto &name_info_pair : map) {
        if (name_info_pair.second.throttling_info->binded_cdev_info_map.size()) {
            *dump_buf << " Name: " << name_info_pair.first << std::endl;
            *dump_buf << "  PID Info:" << std::endl;
            *dump_buf << "   K_po: [";
            for (size_t i = 0; i < kThrottlingSeverityCount; ++i) {
                *dump_buf << name_info_pair.second.throttling_info->k_po[i] << " ";
            }
            *dump_buf << "]" << std::endl;
            *dump_buf << "   K_pu: [";
            for (size_t i = 0; i < kThrottlingSeverityCount; ++i) {
                *dump_buf << name_info_pair.second.throttling_info->k_pu[i] << " ";
            }
            *dump_buf << "]" << std::endl;
            *dump_buf << "   K_i: [";
            for (size_t i = 0; i < kThrottlingSeverityCount; ++i) {
                *dump_buf << name_info_pair.second.throttling_info->k_i[i] << " ";
            }
            *dump_buf << "]" << std::endl;
            *dump_buf << "   K_d: [";
            for (size_t i = 0; i < kThrottlingSeverityCount; ++i) {
                *dump_buf << name_info_pair.second.throttling_info->k_d[i] << " ";
            }
            *dump_buf << "]" << std::endl;
            *dump_buf << "   i_max: [";
            for (size_t i = 0; i < kThrottlingSeverityCount; ++i) {
                *dump_buf << name_info_pair.second.throttling_info->i_max[i] << " ";
            }
            *dump_buf << "]" << std::endl;
            *dump_buf << "   max_alloc_power: [";
            for (size_t i = 0; i < kThrottlingSeverityCount; ++i) {
                *dump_buf << name_info_pair.second.throttling_info->max_alloc_power[i] << " ";
            }
            *dump_buf << "]" << std::endl;
            *dump_buf << "   min_alloc_power: [";
            for (size_t i = 0; i < kThrottlingSeverityCount; ++i) {
                *dump_buf << name_info_pair.second.throttling_info->min_alloc_power[i] << " ";
            }
            *dump_buf << "]" << std::endl;
            *dump_buf << "   s_power: [";
            for (size_t i = 0; i < kThrottlingSeverityCount; ++i) {
                *dump_buf << name_info_pair.second.throttling_info->s_power[i] << " ";
            }
            *dump_buf << "]" << std::endl;
            *dump_buf << "   i_cutoff: [";
            for (size_t i = 0; i < kThrottlingSeverityCount; ++i) {
                *dump_buf << name_info_pair.second.throttling_info->i_cutoff[i] << " ";
            }
            *dump_buf << "]" << std::endl;
            *dump_buf << "  Binded CDEV Info:" << std::endl;
            if (name_info_pair.second.throttling_info->binded_cdev_info_map.size()) {
                for (const auto &binded_cdev_info_pair :
                     name_info_pair.second.throttling_info->binded_cdev_info_map) {
                    *dump_buf << "   Cooling device name: " << binded_cdev_info_pair.first
                              << std::endl;
                    *dump_buf << "    WeightForPID: [";
                    for (size_t i = 0; i < kThrottlingSeverityCount; ++i) {
                        *dump_buf << binded_cdev_info_pair.second.cdev_weight_for_pid[i] << " ";
                    }
                    *dump_buf << "]" << std::endl;
                    *dump_buf << "    Ceiling: [";
                    for (size_t i = 0; i < kThrottlingSeverityCount; ++i) {
                        *dump_buf << binded_cdev_info_pair.second.cdev_ceiling[i] << " ";
                    }
                    *dump_buf << "]" << std::endl;
                    *dump_buf << "    Floor with PowerLink: [";
                    for (size_t i = 0; i < kThrottlingSeverityCount; ++i) {
                        *dump_buf << binded_cdev_info_pair.second.cdev_floor_with_power_link[i]
                                  << " ";
                    }
                    *dump_buf << "]" << std::endl;
                    *dump_buf << "    Hard limit: [";
                    for (size_t i = 0; i < kThrottlingSeverityCount; ++i) {
                        *dump_buf << binded_cdev_info_pair.second.limit_info[i] << " ";
                    }
                    *dump_buf << "]" << std::endl;

                    if (!binded_cdev_info_pair.second.power_rail.empty()) {
                        *dump_buf << "    Binded power rail: "
                                  << binded_cdev_info_pair.second.power_rail << std::endl;
                        *dump_buf << "    Power threshold: [";
                        for (size_t i = 0; i < kThrottlingSeverityCount; ++i) {
                            *dump_buf << binded_cdev_info_pair.second.power_thresholds[i] << " ";
                        }
                        *dump_buf << "]" << std::endl;
                        *dump_buf << "    Release logic: ";
                        switch (binded_cdev_info_pair.second.release_logic) {
                            case ReleaseLogic::INCREASE:
                                *dump_buf << "INCREASE";
                                break;
                            case ReleaseLogic::DECREASE:
                                *dump_buf << "DECREASE";
                                break;
                            case ReleaseLogic::STEPWISE:
                                *dump_buf << "STEPWISE";
                                break;
                            case ReleaseLogic::RELEASE_TO_FLOOR:
                                *dump_buf << "RELEASE_TO_FLOOR";
                                break;
                            default:
                                *dump_buf << "NONE";
                                break;
                        }
                        *dump_buf << std::endl;
                        *dump_buf << "    high_power_check: " << std::boolalpha
                                  << binded_cdev_info_pair.second.high_power_check << std::endl;
                        *dump_buf << "    throttling_with_power_link: " << std::boolalpha
                                  << binded_cdev_info_pair.second.throttling_with_power_link
                                  << std::endl;
                    }
                }
            }
        }
    }
}

void Thermal::dumpThrottlingRequestStatus(std::ostringstream *dump_buf) {
    const auto &sensor_status_map = thermal_helper_.GetSensorStatusMap();
    const auto &cdev_status_map = thermal_helper_.GetCdevStatusMap();
    const auto &release_map = thermal_helper_.GetThrottlingReleaseMap();
    *dump_buf << "Throttling Request Status " << std::endl;
    for (const auto &cdev_status_pair : cdev_status_map) {
        *dump_buf << " Name: " << cdev_status_pair.first << std::endl;
        for (const auto &request_pair : cdev_status_pair.second) {
            *dump_buf << "  Request Sensor: " << request_pair.first << std::endl;
            *dump_buf << "   Request Throttling State: " << request_pair.second << std::endl;
            if (sensor_status_map.at(request_pair.first).pid_request_map.size() &&
                sensor_status_map.at(request_pair.first)
                        .pid_request_map.count(cdev_status_pair.first)) {
                *dump_buf << "   PID Request State: "
                          << sensor_status_map.at(request_pair.first)
                                     .pid_request_map.at(cdev_status_pair.first)
                          << std::endl;
            }
            if (sensor_status_map.at(request_pair.first).hard_limit_request_map.size() &&
                sensor_status_map.at(request_pair.first)
                        .hard_limit_request_map.count(cdev_status_pair.first)) {
                *dump_buf << "   Hard Limit Request State: "
                          << sensor_status_map.at(request_pair.first)
                                     .hard_limit_request_map.at(cdev_status_pair.first)
                          << std::endl;
            }
            if (release_map.count(request_pair.first) &&
                release_map.at(request_pair.first).count(cdev_status_pair.first)) {
                const auto &cdev_release_info =
                        release_map.at(request_pair.first).at(cdev_status_pair.first);
                *dump_buf << "   Release Step: " << cdev_release_info.release_step << std::endl;
            }
        }
    }
}

void Thermal::dumpPowerRailInfo(std::ostringstream *dump_buf) {
    const auto &power_rail_info_map = thermal_helper_.GetPowerRailInfoMap();
    const auto &power_status_map = thermal_helper_.GetPowerStatusMap();

    *dump_buf << "Power Rail Info " << std::endl;
    for (const auto &power_rail_pair : power_rail_info_map) {
        *dump_buf << " Power Rail: " << power_rail_pair.first << std::endl;
        *dump_buf << "  Power Sample Count: " << power_rail_pair.second.power_sample_count
                  << std::endl;
        *dump_buf << "  Power Sample Delay: " << power_rail_pair.second.power_sample_delay.count()
                  << std::endl;
        for (const auto &power_status_pair : power_status_map) {
            if (power_status_pair.second.count(power_rail_pair.first)) {
                auto power_history =
                        power_status_pair.second.at(power_rail_pair.first).power_history;
                *dump_buf << "  Request Sensor: " << power_status_pair.first << std::endl;
                *dump_buf
                        << "   Last Updated AVG Power: "
                        << power_status_pair.second.at(power_rail_pair.first).last_updated_avg_power
                        << " mW" << std::endl;
                if (power_rail_pair.second.virtual_power_rail_info != nullptr) {
                    *dump_buf << "   Formula=";
                    switch (power_rail_pair.second.virtual_power_rail_info->formula) {
                        case FormulaOption::COUNT_THRESHOLD:
                            *dump_buf << "COUNT_THRESHOLD";
                            break;
                        case FormulaOption::WEIGHTED_AVG:
                            *dump_buf << "WEIGHTED_AVG";
                            break;
                        case FormulaOption::MAXIMUM:
                            *dump_buf << "MAXIMUM";
                            break;
                        case FormulaOption::MINIMUM:
                            *dump_buf << "MINIMUM";
                            break;
                        default:
                            *dump_buf << "NONE";
                            break;
                    }
                    *dump_buf << std::endl;
                }
                for (size_t i = 0; i < power_history.size(); ++i) {
                    if (power_rail_pair.second.virtual_power_rail_info != nullptr) {
                        *dump_buf << "   Linked power rail "
                                  << power_rail_pair.second.virtual_power_rail_info
                                             ->linked_power_rails[i]
                                  << std::endl;
                        *dump_buf << "    Coefficient="
                                  << power_rail_pair.second.virtual_power_rail_info->coefficients[i]
                                  << std::endl;
                        *dump_buf << "    Power Samples: ";
                    } else {
                        *dump_buf << "   Power Samples: ";
                    }
                    while (power_history[i].size() > 0) {
                        const auto power_sample = power_history[i].front();
                        power_history[i].pop();
                        *dump_buf << "(T=" << power_sample.duration
                                  << ", uWs=" << power_sample.energy_counter << ") ";
                    }
                    *dump_buf << std::endl;
                }
            }
        }
    }
}

Return<void> Thermal::debug(const hidl_handle &handle, const hidl_vec<hidl_string> &) {
    if (handle != nullptr && handle->numFds >= 1) {
        int fd = handle->data[0];
        std::ostringstream dump_buf;

        if (!thermal_helper_.isInitializedOk()) {
            dump_buf << "ThermalHAL not initialized properly." << std::endl;
        } else {
            {
                hidl_vec<Temperature_1_0> temperatures;
                dump_buf << "getTemperatures:" << std::endl;
                if (!thermal_helper_.fillTemperatures(&temperatures)) {
                    dump_buf << "Failed to read thermal sensors." << std::endl;
                }

                for (const auto &t : temperatures) {
                    dump_buf << " Type: " << android::hardware::thermal::V1_0::toString(t.type)
                             << " Name: " << t.name << " CurrentValue: " << t.currentValue
                             << " ThrottlingThreshold: " << t.throttlingThreshold
                             << " ShutdownThreshold: " << t.shutdownThreshold
                             << " VrThrottlingThreshold: " << t.vrThrottlingThreshold << std::endl;
                }
            }
            {
                hidl_vec<CpuUsage> cpu_usages;
                dump_buf << "getCpuUsages:" << std::endl;
                if (!thermal_helper_.fillCpuUsages(&cpu_usages)) {
                    dump_buf << "Failed to get CPU usages." << std::endl;
                }

                for (const auto &usage : cpu_usages) {
                    dump_buf << " Name: " << usage.name << " Active: " << usage.active
                             << " Total: " << usage.total << " IsOnline: " << usage.isOnline
                             << std::endl;
                }
            }
            {
                dump_buf << "getCurrentTemperatures:" << std::endl;
                hidl_vec<Temperature_2_0> temperatures;
                if (!thermal_helper_.fillCurrentTemperatures(false, TemperatureType_2_0::SKIN,
                                                             &temperatures)) {
                    dump_buf << "Failed to getCurrentTemperatures." << std::endl;
                }

                for (const auto &t : temperatures) {
                    dump_buf << " Type: " << android::hardware::thermal::V2_0::toString(t.type)
                             << " Name: " << t.name << " CurrentValue: " << t.value
                             << " ThrottlingStatus: "
                             << android::hardware::thermal::V2_0::toString(t.throttlingStatus)
                             << std::endl;
                }
            }
            {
                dump_buf << "getTemperatureThresholds:" << std::endl;
                hidl_vec<TemperatureThreshold> temperatures;
                if (!thermal_helper_.fillTemperatureThresholds(false, TemperatureType_2_0::SKIN,
                                                               &temperatures)) {
                    dump_buf << "Failed to getTemperatureThresholds." << std::endl;
                }

                for (const auto &t : temperatures) {
                    dump_buf << " Type: " << android::hardware::thermal::V2_0::toString(t.type)
                             << " Name: " << t.name;
                    dump_buf << " hotThrottlingThreshold: [";
                    for (size_t i = 0; i < kThrottlingSeverityCount; ++i) {
                        dump_buf << t.hotThrottlingThresholds[i] << " ";
                    }
                    dump_buf << "] coldThrottlingThreshold: [";
                    for (size_t i = 0; i < kThrottlingSeverityCount; ++i) {
                        dump_buf << t.coldThrottlingThresholds[i] << " ";
                    }
                    dump_buf << "] vrThrottlingThreshold: " << t.vrThrottlingThreshold;
                    dump_buf << std::endl;
                }
            }
            {
                dump_buf << "getCurrentCoolingDevices:" << std::endl;
                hidl_vec<CoolingDevice_2_0> cooling_devices;
                if (!thermal_helper_.fillCurrentCoolingDevices(false, CoolingType::CPU,
                                                               &cooling_devices)) {
                    dump_buf << "Failed to getCurrentCoolingDevices." << std::endl;
                }

                for (const auto &c : cooling_devices) {
                    dump_buf << " Type: " << android::hardware::thermal::V2_0::toString(c.type)
                             << " Name: " << c.name << " CurrentValue: " << c.value << std::endl;
                }
            }
            {
                dump_buf << "Callbacks: Total " << callbacks_.size() << std::endl;
                for (const auto &c : callbacks_) {
                    dump_buf << " IsFilter: " << c.is_filter_type
                             << " Type: " << android::hardware::thermal::V2_0::toString(c.type)
                             << std::endl;
                }
            }
            {
                dump_buf << "getHysteresis:" << std::endl;
                const auto &map = thermal_helper_.GetSensorInfoMap();
                for (const auto &name_info_pair : map) {
                    dump_buf << " Name: " << name_info_pair.first;
                    dump_buf << " hotHysteresis: [";
                    for (size_t i = 0; i < kThrottlingSeverityCount; ++i) {
                        dump_buf << name_info_pair.second.hot_hysteresis[i] << " ";
                    }
                    dump_buf << "] coldHysteresis: [";
                    for (size_t i = 0; i < kThrottlingSeverityCount; ++i) {
                        dump_buf << name_info_pair.second.cold_hysteresis[i] << " ";
                    }
                    dump_buf << "]" << std::endl;
                }
            }
            {
                dump_buf << "SendCallback" << std::endl;
                dump_buf << "  Enabled List: ";
                const auto &map = thermal_helper_.GetSensorInfoMap();
                for (const auto &name_info_pair : map) {
                    if (name_info_pair.second.send_cb) {
                        dump_buf << name_info_pair.first << " ";
                    }
                }
                dump_buf << std::endl;
            }
            {
                dump_buf << "SendPowerHint" << std::endl;
                dump_buf << "  Enabled List: ";
                const auto &map = thermal_helper_.GetSensorInfoMap();
                for (const auto &name_info_pair : map) {
                    if (name_info_pair.second.send_powerhint) {
                        dump_buf << name_info_pair.first << " ";
                    }
                }
                dump_buf << std::endl;
            }
            dumpVirtualSensorInfo(&dump_buf);
            dumpThrottlingInfo(&dump_buf);
            dumpThrottlingRequestStatus(&dump_buf);
            dumpPowerRailInfo(&dump_buf);
            {
                dump_buf << "AIDL Power Hal exist: " << std::boolalpha
                         << thermal_helper_.isAidlPowerHalExist() << std::endl;
                dump_buf << "AIDL Power Hal connected: " << std::boolalpha
                         << thermal_helper_.isPowerHalConnected() << std::endl;
                dump_buf << "AIDL Power Hal Ext connected: " << std::boolalpha
                         << thermal_helper_.isPowerHalExtConnected() << std::endl;
            }
        }
        std::string buf = dump_buf.str();
        if (!android::base::WriteStringToFd(buf, fd)) {
            PLOG(ERROR) << "Failed to dump state to fd";
        }
        fsync(fd);
    }
    return Void();
}

}  // namespace implementation
}  // namespace V2_0
}  // namespace thermal
}  // namespace hardware
}  // namespace android
