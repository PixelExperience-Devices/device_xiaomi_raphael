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
#include <stdlib.h>
#include <sys/sysinfo.h>
#include <sys/system_properties.h>
#include <sys/_system_properties.h>

#include "property_service.h"
#include "vendor_init.h"

void property_override(char const prop[], char const value[], bool add = true) {
    prop_info *pi;

    pi = (prop_info *)__system_property_find(prop);
    if (pi)
        __system_property_update(pi, value, strlen(value));
    else if (add)
        __system_property_add(prop, strlen(prop), value, strlen(value));
}

void load_dalvikvm_properties() {
  struct sysinfo sys;
  sysinfo(&sys);
  if (sys.totalram > 8192ull * 1024 * 1024) {
    // from - phone-xhdpi-12288-dalvik-heap.mk
    property_override("dalvik.vm.heapstartsize", "24m");
    property_override("dalvik.vm.heapgrowthlimit", "384m");
    property_override("dalvik.vm.heaptargetutilization", "0.42");
    property_override("dalvik.vm.heapmaxfree", "56m");
    }
  else if(sys.totalram > 6144ull * 1024 * 1024) {
    // from - phone-xhdpi-8192-dalvik-heap.mk
    property_override("dalvik.vm.heapstartsize", "24m");
    property_override("dalvik.vm.heapgrowthlimit", "256m");
    property_override("dalvik.vm.heaptargetutilization", "0.46");
    property_override("dalvik.vm.heapmaxfree", "48m");
    }
  else {
    // from - phone-xhdpi-6144-dalvik-heap.mk
    property_override("dalvik.vm.heapstartsize", "16m");
    property_override("dalvik.vm.heapgrowthlimit", "256m");
    property_override("dalvik.vm.heaptargetutilization", "0.5");
    property_override("dalvik.vm.heapmaxfree", "32m");
  }
  property_override("dalvik.vm.heapsize", "512m");
  property_override("dalvik.vm.heapminfree", "8m");
}

std::vector<std::string> ro_props_default_source_order = {
        "", "bootimage.", "odm.", "product.", "system.", "system_ext.", "vendor.",
};

void set_ro_build_prop(const std::string& prop, const std::string& value) {
    for (const auto& source : ro_props_default_source_order) {
        auto prop_name = "ro." + source + "build." + prop;
        if (source == "")
            property_override(prop_name.c_str(), value.c_str());
        else
            property_override(prop_name.c_str(), value.c_str(), false);
    }
};

void set_ro_product_prop(const std::string& prop, const std::string& value) {
    for (const auto& source : ro_props_default_source_order) {
        auto prop_name = "ro.product." + source + prop;
        property_override(prop_name.c_str(), value.c_str(), false);
    }
};

void vendor_load_properties() {
    std::string region = android::base::GetProperty("ro.boot.hwc", "GLOBAL");
    std::string hardware_revision = android::base::GetProperty("ro.boot.hwversion", "UNKNOWN");

    std::string model;
    std::string device;
    std::string fingerprint;
    std::string description;
    std::string mod_device;

    if (region == "GLOBAL") {
        model = "Mi 9T Pro";
        device = "raphael";
        description = "raphael-user 11 RKQ1.200826.002 V12.5.2.0.RFKMIXM release-keys";
        mod_device = "raphael_global";
    } else if (region == "CN") {
        model = "Redmi K20 Pro";
        device = "raphael";
        description = "raphael-user 11 RKQ1.200826.002 V12.5.5.0.RFKCNXM release-keys";
    } else if (region == "INDIA") {
        model = "Redmi K20 Pro";
        device = "raphaelin";
        description = "raphaelin-user 11 RKQ1.200826.002 V12.5.1.0.RFKINXM release-keys";
        mod_device = "raphaelin_in_global";
    }

    set_ro_product_prop("device", device);
    set_ro_product_prop("model", model);
    property_override("ro.build.description", description.c_str());
    if (mod_device != "") {
        property_override("ro.product.mod_device", mod_device.c_str());
    }

    property_override("ro.boot.hardware.revision", hardware_revision.c_str());

    // dalvikvm props
    load_dalvikvm_properties();
}
