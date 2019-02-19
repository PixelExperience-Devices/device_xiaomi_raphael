#
# Copyright (C) 2019 The PixelExperience Project
#
# SPDX-License-Identifier: Apache-2.0
#

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := $(call all-java-files-under, src)

LOCAL_PACKAGE_NAME := ThermalController
LOCAL_CERTIFICATE := platform
LOCAL_PRIVATE_PLATFORM_APIS := true
LOCAL_PRIVILEGED_MODULE := true

LOCAL_USE_AAPT2 := true

include $(BUILD_PACKAGE)
