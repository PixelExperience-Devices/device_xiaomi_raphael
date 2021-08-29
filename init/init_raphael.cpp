/*
 * Copyright (c) 2021, The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cstdlib>
#include <fstream>
#include <string.h>
#include <unistd.h>
#include <vector>

#include <android-base/properties.h>
#define _REALLY_INCLUDE_SYS__SYSTEM_PROPERTIES_H_
#include <sys/_system_properties.h>

#include "property_service.h"
#include "vendor_init.h"

using android::base::GetProperty;
using std::string;

std::vector<std::string> ro_props_default_source_order = {
    "",
    "odm.",
    "product.",
    "system.",
    "system_ext.",
    "vendor.",
};

void property_override(char const prop[], char const value[], bool add = true) {
    prop_info *pi;

    pi = (prop_info *)__system_property_find(prop);
    if (pi)
        __system_property_update(pi, value, strlen(value));
    else if (add)
        __system_property_add(prop, strlen(prop), value, strlen(value));
}

void set_device_props(const std::string fingerprint, const std::string description,
        const std::string brand, const std::string device, const std::string model) {
    const auto set_ro_build_prop = [](const std::string &source,
                                      const std::string &prop,
                                      const std::string &value) {
        auto prop_name = "ro." + source + "build." + prop;
        property_override(prop_name.c_str(), value.c_str(), false);
    };

    const auto set_ro_product_prop = [](const std::string &source,
                                        const std::string &prop,
                                        const std::string &value) {
        auto prop_name = "ro.product." + source + prop;
        property_override(prop_name.c_str(), value.c_str(), false);
    };

    for (const auto &source : ro_props_default_source_order) {
        set_ro_build_prop(source, "fingerprint", fingerprint);
        set_ro_product_prop(source, "brand", brand);
        set_ro_product_prop(source, "device", device);
        set_ro_product_prop(source, "model", model);
    }

    property_override("ro.build.fingerprint", fingerprint.c_str());
    property_override("ro.build.description", description.c_str());
    property_override("ro.bootimage.build.fingerprint", fingerprint.c_str());
    property_override("ro.system_ext.build.fingerprint", fingerprint.c_str());
}

void vendor_load_properties() {
    char const fp[] = "Xiaomi/raphael/raphael:11/RKQ1.200826.002/V12.5.1.0.RFKMIXM:user/release-keys";
    char const fp_desc[] = "raphael-user 11 RKQ1.200826.002 V12.5.1.0.RFKMIXM release-keys";

    string region = android::base::GetProperty("ro.boot.hwc", "");

    if (region == "INDIA") {
        set_device_props(
            fp,
            fp_desc,
            "Xiaomi", "raphaelin", "Redmi K20 Pro");
    } else if (region == "CN") {
        set_device_props(
            fp,
            fp_desc,
            "Xiaomi", "raphael", "Redmi K20 Pro");
    } else {
        set_device_props(
            fp,
            fp_desc,
            "Xiaomi", "raphael", "Mi 9T Pro");
    }
}
