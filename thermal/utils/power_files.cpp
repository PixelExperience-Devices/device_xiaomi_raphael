/*
 * Copyright (C) 2021 The Android Open Source Project
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
#include <dirent.h>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>

#include "power_files.h"

namespace android {
namespace hardware {
namespace thermal {
namespace V2_0 {
namespace implementation {

constexpr std::string_view kDeviceType("iio:device");
constexpr std::string_view kIioRootDir("/sys/bus/iio/devices");
constexpr std::string_view kEnergyValueNode("energy_value");

using android::base::ReadFileToString;
using android::base::StringPrintf;

void PowerFiles::setPowerDataToDefault(std::string_view sensor_name) {
    std::unique_lock<std::shared_mutex> _lock(throttling_release_map_mutex_);
    if (!throttling_release_map_.count(sensor_name.data()) ||
        !power_status_map_.count(sensor_name.data())) {
        return;
    }

    auto &cdev_release_map = throttling_release_map_.at(sensor_name.data());
    PowerSample power_sample = {};

    for (auto &power_status_pair : power_status_map_.at(sensor_name.data())) {
        for (size_t i = 0; i < power_status_pair.second.power_history.size(); ++i) {
            for (size_t j = 0; j < power_status_pair.second.power_history[i].size(); ++j) {
                power_status_pair.second.power_history[i].pop();
                power_status_pair.second.power_history[i].emplace(power_sample);
            }
        }
        power_status_pair.second.last_updated_avg_power = NAN;
    }

    for (auto &cdev_release_pair : cdev_release_map) {
        cdev_release_pair.second.release_step = 0;
    }
}

int PowerFiles::getReleaseStep(std::string_view sensor_name, std::string_view cdev_name) {
    int release_step = 0;
    std::shared_lock<std::shared_mutex> _lock(throttling_release_map_mutex_);

    if (throttling_release_map_.count(sensor_name.data()) &&
        throttling_release_map_[sensor_name.data()].count(cdev_name.data())) {
        release_step = throttling_release_map_[sensor_name.data()][cdev_name.data()].release_step;
    }

    return release_step;
}

bool PowerFiles::registerPowerRailsToWatch(std::string_view sensor_name, std::string_view cdev_name,
                                           const BindedCdevInfo &binded_cdev_info,
                                           const CdevInfo &cdev_info,
                                           const PowerRailInfo &power_rail_info) {
    std::vector<std::queue<PowerSample>> power_history;
    PowerSample power_sample = {
            .energy_counter = 0,
            .duration = 0,
    };

    if (throttling_release_map_.count(sensor_name.data()) &&
        throttling_release_map_[sensor_name.data()].count(binded_cdev_info.power_rail)) {
        return true;
    }

    if (!energy_info_map_.size() && !updateEnergyValues()) {
        LOG(ERROR) << "Faield to update energy info";
        return false;
    }

    if (power_rail_info.virtual_power_rail_info != nullptr &&
        power_rail_info.virtual_power_rail_info->linked_power_rails.size()) {
        for (size_t i = 0; i < power_rail_info.virtual_power_rail_info->linked_power_rails.size();
             ++i) {
            if (energy_info_map_.count(
                        power_rail_info.virtual_power_rail_info->linked_power_rails[i])) {
                power_history.emplace_back(std::queue<PowerSample>());
                for (int j = 0; j < power_rail_info.power_sample_count; j++) {
                    power_history[i].emplace(power_sample);
                }
            }
        }
    } else {
        if (energy_info_map_.count(power_rail_info.rail)) {
            power_history.emplace_back(std::queue<PowerSample>());
            for (int j = 0; j < power_rail_info.power_sample_count; j++) {
                power_history[0].emplace(power_sample);
            }
        }
    }

    if (power_history.size()) {
        throttling_release_map_[sensor_name.data()][cdev_name.data()] = {
                .release_step = 0,
                .max_release_step = cdev_info.max_state,
        };
        power_status_map_[sensor_name.data()][binded_cdev_info.power_rail] = {
                .power_history = power_history,
                .time_remaining = power_rail_info.power_sample_delay,
                .last_updated_avg_power = NAN,
        };
    } else {
        return false;
    }

    LOG(INFO) << "Sensor " << sensor_name.data() << " successfully registers power rail "
              << binded_cdev_info.power_rail << " for cooling device " << cdev_name.data();
    return true;
}

bool PowerFiles::findEnergySourceToWatch(void) {
    std::string devicePath;

    if (energy_path_set_.size()) {
        return true;
    }

    std::unique_ptr<DIR, decltype(&closedir)> dir(opendir(kIioRootDir.data()), closedir);
    if (!dir) {
        PLOG(ERROR) << "Error opening directory" << kIioRootDir;
        return false;
    }

    // Find any iio:devices that support energy_value
    while (struct dirent *ent = readdir(dir.get())) {
        std::string devTypeDir = ent->d_name;
        if (devTypeDir.find(kDeviceType) != std::string::npos) {
            devicePath = StringPrintf("%s/%s", kIioRootDir.data(), devTypeDir.data());
            std::string deviceEnergyContent;

            if (!ReadFileToString(StringPrintf("%s/%s", devicePath.data(), kEnergyValueNode.data()),
                                  &deviceEnergyContent)) {
            } else if (deviceEnergyContent.size()) {
                energy_path_set_.emplace(
                        StringPrintf("%s/%s", devicePath.data(), kEnergyValueNode.data()));
            }
        }
    }

    if (!energy_path_set_.size()) {
        return false;
    }

    return true;
}

void PowerFiles::clearEnergyInfoMap(void) {
    energy_info_map_.clear();
}

bool PowerFiles::updateEnergyValues(void) {
    std::string deviceEnergyContent;
    std::string deviceEnergyContents;
    std::string line;

    for (const auto &path : energy_path_set_) {
        if (!android::base::ReadFileToString(path, &deviceEnergyContent)) {
            LOG(ERROR) << "Failed to read energy content from " << path;
            return false;
        } else {
            deviceEnergyContents.append(deviceEnergyContent);
        }
    }

    std::istringstream energyData(deviceEnergyContents);

    clearEnergyInfoMap();
    while (std::getline(energyData, line)) {
        /* Read rail energy */
        uint64_t energy_counter = 0;
        uint64_t duration = 0;

        /* Format example: CH3(T=358356)[S2M_VDD_CPUCL2], 761330 */
        auto start_pos = line.find("T=");
        auto end_pos = line.find(')');
        if (start_pos != std::string::npos) {
            duration =
                    strtoul(line.substr(start_pos + 2, end_pos - start_pos - 2).c_str(), NULL, 10);
        } else {
            continue;
        }

        start_pos = line.find(")[");
        end_pos = line.find(']');
        std::string railName;
        if (start_pos != std::string::npos) {
            railName = line.substr(start_pos + 2, end_pos - start_pos - 2);
        } else {
            continue;
        }

        start_pos = line.find("],");
        if (start_pos != std::string::npos) {
            energy_counter = strtoul(line.substr(start_pos + 2).c_str(), NULL, 10);
        } else {
            continue;
        }

        energy_info_map_[railName] = {
                .energy_counter = energy_counter,
                .duration = duration,
        };
    }

    return true;
}

bool PowerFiles::getAveragePower(std::string_view power_rail,
                                 std::queue<PowerSample> *power_history, bool power_sample_update,
                                 float *avg_power) {
    const auto curr_sample = energy_info_map_.at(power_rail.data());
    bool ret = true;

    const auto last_sample = power_history->front();
    const auto duration = curr_sample.duration - last_sample.duration;
    const auto deltaEnergy = curr_sample.energy_counter - last_sample.energy_counter;

    if (!last_sample.duration) {
        LOG(VERBOSE) << "Power rail " << power_rail.data() << ": the last energy timestamp is zero";
    } else if (duration <= 0 || deltaEnergy < 0) {
        LOG(ERROR) << "Power rail " << power_rail.data() << " is invalid: duration = " << duration
                   << ", deltaEnergy = " << deltaEnergy;

        ret = false;
    } else {
        *avg_power = static_cast<float>(deltaEnergy) / static_cast<float>(duration);
        LOG(VERBOSE) << "Power rail " << power_rail.data() << ", avg power = " << *avg_power
                     << ", duration = " << duration << ", deltaEnergy = " << deltaEnergy;
    }

    if (power_sample_update) {
        power_history->pop();
        power_history->push(curr_sample);
    }

    return ret;
}

bool PowerFiles::computeAveragePower(const PowerRailInfo &power_rail_info,
                                     PowerStatus *power_status, bool power_sample_update,
                                     float *avg_power) {
    bool ret = true;

    float avg_power_val = -1;
    float offset = power_rail_info.virtual_power_rail_info->offset;
    for (size_t i = 0; i < power_rail_info.virtual_power_rail_info->linked_power_rails.size();
         i++) {
        float coefficient = power_rail_info.virtual_power_rail_info->coefficients[i];
        float avg_power_number = -1;
        if (!getAveragePower(power_rail_info.virtual_power_rail_info->linked_power_rails[i],
                             &power_status->power_history[i], power_sample_update,
                             &avg_power_number)) {
            ret = false;
            continue;
        } else if (avg_power_number < 0) {
            continue;
        }
        switch (power_rail_info.virtual_power_rail_info->formula) {
            case FormulaOption::COUNT_THRESHOLD:
                if ((coefficient < 0 && avg_power_number < -coefficient) ||
                    (coefficient >= 0 && avg_power_number >= coefficient))
                    avg_power_val += 1;
                break;
            case FormulaOption::WEIGHTED_AVG:
                avg_power_val += avg_power_number * coefficient;
                break;
            case FormulaOption::MAXIMUM:
                if (i == 0)
                    avg_power_val = std::numeric_limits<float>::lowest();
                if (avg_power_number * coefficient > avg_power_val)
                    avg_power_val = avg_power_number * coefficient;
                break;
            case FormulaOption::MINIMUM:
                if (i == 0)
                    avg_power_val = std::numeric_limits<float>::max();
                if (avg_power_number * coefficient < avg_power_val)
                    avg_power_val = avg_power_number * coefficient;
                break;
            default:
                break;
        }
    }
    if (avg_power_val >= 0) {
        avg_power_val = avg_power_val + offset;
    }

    *avg_power = avg_power_val;
    return ret;
}

bool PowerFiles::throttlingReleaseUpdate(std::string_view sensor_name, std::string_view cdev_name,
                                         const ThrottlingSeverity severity,
                                         const std::chrono::milliseconds time_elapsed_ms,
                                         const BindedCdevInfo &binded_cdev_info,
                                         const PowerRailInfo &power_rail_info,
                                         bool power_sample_update, bool severity_changed) {
    std::unique_lock<std::shared_mutex> _lock(throttling_release_map_mutex_);
    float avg_power = -1;

    if (!throttling_release_map_.count(sensor_name.data()) ||
        !throttling_release_map_[sensor_name.data()].count(cdev_name.data()) ||
        !power_status_map_.count(sensor_name.data()) ||
        !power_status_map_[sensor_name.data()].count(binded_cdev_info.power_rail)) {
        return false;
    }

    auto &release_status = throttling_release_map_[sensor_name.data()].at(cdev_name.data());
    auto &power_status = power_status_map_[sensor_name.data()].at(binded_cdev_info.power_rail);

    if (power_sample_update) {
        if (time_elapsed_ms > power_status.time_remaining) {
            power_status.time_remaining = power_rail_info.power_sample_delay;
        } else {
            power_status.time_remaining = power_status.time_remaining - time_elapsed_ms;
            LOG(VERBOSE) << "Power rail " << binded_cdev_info.power_rail
                         << " : timeout remaining = " << power_status.time_remaining.count();
            if (!severity_changed) {
                return true;
            } else {
                // get the cached average power when thermal severity is changed
                power_sample_update = false;
            }
        }
    } else if (!severity_changed &&
               power_status.time_remaining != power_rail_info.power_sample_delay) {
        return false;
    }

    if (!energy_info_map_.size() && !updateEnergyValues()) {
        LOG(ERROR) << "Failed to update energy values";
        release_status.release_step = 0;
        return false;
    }

    if (!power_sample_update && !std::isnan(power_status.last_updated_avg_power)) {
        avg_power = power_status.last_updated_avg_power;
    } else {
        // Return false if we cannot get the average power of the target power rail
        if (!((power_rail_info.virtual_power_rail_info == nullptr)
                      ? getAveragePower(binded_cdev_info.power_rail, &power_status.power_history[0],
                                        power_sample_update, &avg_power)
                      : computeAveragePower(power_rail_info, &power_status, power_sample_update,
                                            &avg_power))) {
            release_status.release_step = 0;
            if (binded_cdev_info.throttling_with_power_link) {
                release_status.release_step = release_status.max_release_step;
            }
            return false;
        } else if (avg_power < 0) {
            if (binded_cdev_info.throttling_with_power_link) {
                release_status.release_step = release_status.max_release_step;
            }
            return true;
        }
    }

    power_status.last_updated_avg_power = avg_power;
    bool is_over_budget = true;
    if (!binded_cdev_info.high_power_check) {
        if (avg_power < binded_cdev_info.power_thresholds[static_cast<int>(severity)]) {
            is_over_budget = false;
        }
    } else {
        if (avg_power > binded_cdev_info.power_thresholds[static_cast<int>(severity)]) {
            is_over_budget = false;
        }
    }
    LOG(INFO) << "Power rail " << binded_cdev_info.power_rail << ": power threshold = "
              << binded_cdev_info.power_thresholds[static_cast<int>(severity)]
              << ", avg power = " << avg_power;

    switch (binded_cdev_info.release_logic) {
        case ReleaseLogic::INCREASE:
            if (!is_over_budget) {
                if (std::abs(release_status.release_step) <
                    static_cast<int>(release_status.max_release_step)) {
                    release_status.release_step--;
                }
            } else {
                release_status.release_step = 0;
            }
            break;
        case ReleaseLogic::DECREASE:
            if (!is_over_budget) {
                if (release_status.release_step <
                    static_cast<int>(release_status.max_release_step)) {
                    release_status.release_step++;
                }
            } else {
                release_status.release_step = 0;
            }
            break;
        case ReleaseLogic::STEPWISE:
            if (!is_over_budget) {
                if (release_status.release_step <
                    static_cast<int>(release_status.max_release_step)) {
                    release_status.release_step++;
                }
            } else {
                if (std::abs(release_status.release_step) <
                    static_cast<int>(release_status.max_release_step)) {
                    release_status.release_step--;
                }
            }
            break;
        case ReleaseLogic::RELEASE_TO_FLOOR:
            release_status.release_step = is_over_budget ? 0 : release_status.max_release_step;
            break;
        case ReleaseLogic::NONE:
        default:
            break;
    }
    return true;
}

}  // namespace implementation
}  // namespace V2_0
}  // namespace thermal
}  // namespace hardware
}  // namespace android
