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

#pragma once

#include <queue>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "config_parser.h"

namespace android {
namespace hardware {
namespace thermal {
namespace V2_0 {
namespace implementation {

struct PowerSample {
    uint64_t energy_counter;
    uint64_t duration;
};

struct ReleaseStatus {
    int release_step;
    int max_release_step;
};

struct PowerStatus {
    std::chrono::milliseconds time_remaining;
    // A vector to record the queues of power sample history.
    std::vector<std::queue<PowerSample>> power_history;
    float last_updated_avg_power;
};

using CdevReleaseStatus = std::unordered_map<std::string, ReleaseStatus>;
using PowerStatusMap = std::unordered_map<std::string, PowerStatus>;

// A helper class for monitoring power rails.
class PowerFiles {
  public:
    PowerFiles() = default;
    ~PowerFiles() = default;
    // Disallow copy and assign.
    PowerFiles(const PowerFiles &) = delete;
    void operator=(const PowerFiles &) = delete;

    // Register a map for the throttling release decision of target power rail
    // Return false if the power_rail is not supported.
    bool registerPowerRailsToWatch(std::string_view sensor_name, std::string_view cdev_name,
                                   const BindedCdevInfo &binded_cdev_info,
                                   const CdevInfo &cdev_info, const PowerRailInfo &power_rail_info);

    // Find the energy source path, return false if no energy source found.
    bool findEnergySourceToWatch(void);

    // Clear the data of energy_info_map_.
    void clearEnergyInfoMap(void);

    // Update energy value to energy_info_map_, return false if the value is failed to update.
    bool updateEnergyValues(void);

    bool getAveragePower(std::string_view power_rail, std::queue<PowerSample> *power_history,
                         bool power_sample_update, float *avg_power);
    bool computeAveragePower(const PowerRailInfo &power_rail_info, PowerStatus *power_status,
                             bool power_sample_update, float *avg_power);

    // Update the throttling release status according to the average power, return true if power
    // rail is updated.
    bool throttlingReleaseUpdate(std::string_view sensor_name, std::string_view cdev_name,
                                 const ThrottlingSeverity severity,
                                 const std::chrono::milliseconds time_elapsed_ms,
                                 const BindedCdevInfo &binded_cdev_info,
                                 const PowerRailInfo &power_rail_info, bool power_sample_update,
                                 bool severity_changed);

    // Get the throttling release status for the targer power rail which is binded in specific
    // sensor.
    int getReleaseStep(std::string_view sensor_name, std::string_view cdev_name);

    // Clear the data of throttling_release_map_.
    void setPowerDataToDefault(std::string_view sensor_name);

    // Get throttling release status map
    const std::unordered_map<std::string, CdevReleaseStatus> &GetThrottlingReleaseMap() const {
        std::shared_lock<std::shared_mutex> _lock(throttling_release_map_mutex_);
        return throttling_release_map_;
    }

    // Get Power status map
    const std::unordered_map<std::string, PowerStatusMap> &GetPowerStatusMap() const {
        std::shared_lock<std::shared_mutex> _lock(power_status_map_mutex_);
        return power_status_map_;
    }

  private:
    // The map to record the energy info for each power rail.
    std::unordered_map<std::string, PowerSample> energy_info_map_;
    // The map to record the throttling release status for each thermal sensor.
    std::unordered_map<std::string, CdevReleaseStatus> throttling_release_map_;
    mutable std::shared_mutex throttling_release_map_mutex_;
    // The map to record the power data for each thermal sensor.
    std::unordered_map<std::string, PowerStatusMap> power_status_map_;
    mutable std::shared_mutex power_status_map_mutex_;
    // The set to store the energy source paths
    std::unordered_set<std::string> energy_path_set_;
};

}  // namespace implementation
}  // namespace V2_0
}  // namespace thermal
}  // namespace hardware
}  // namespace android
