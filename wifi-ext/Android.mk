LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := vendor.google.wifi_ext@1.0-service-vendor.raphael
LOCAL_SRC_FILES := ../../../../vendor/google-customization/interfaces/wifi_ext/libs/vendor.google.wifi_ext@1.0-service-vendor
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_OUT_PRODUCT)/vendor_overlay/$(PRODUCT_TARGET_VNDK_VERSION)/bin/hw/
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_OWNER := google
LOCAL_INSTALLED_MODULE_STEM := vendor.google.wifi_ext@1.0-service-vendor
LOCAL_REQUIRED_MODULES := vendor.google.wifi_ext@1.0-service.rc.raphael libwifi-pex.raphael vendor.google.wifi_ext@1.0.raphael
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := vendor.google.wifi_ext@1.0-service.rc.raphael
LOCAL_SRC_FILES := ../../../../vendor/google-customization/interfaces/wifi_ext/libs/vendor.google.wifi_ext@1.0-service.rc
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH := $(TARGET_OUT_PRODUCT)/vendor_overlay/$(PRODUCT_TARGET_VNDK_VERSION)/etc/init/
LOCAL_MODULE_TAGS := optional
LOCAL_INSTALLED_MODULE_STEM := android.hardware.wifi@1.0-service.rc
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := libwifi-pex.raphael
LOCAL_SRC_FILES := ../../../../vendor/google-customization/interfaces/wifi_ext/libs/libwifi-pex.so
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH := $(TARGET_OUT_PRODUCT)/vendor_overlay/$(PRODUCT_TARGET_VNDK_VERSION)/lib64/
LOCAL_MODULE_TAGS := optional
LOCAL_INSTALLED_MODULE_STEM := libwifi-pex.so
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := vendor.google.wifi_ext@1.0.raphael
LOCAL_SRC_FILES := ../../../../vendor/google-customization/interfaces/wifi_ext/libs/vendor.google.wifi_ext@1.0.so
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH := $(TARGET_OUT_PRODUCT)/vendor_overlay/$(PRODUCT_TARGET_VNDK_VERSION)/lib64/
LOCAL_MODULE_TAGS := optional
LOCAL_INSTALLED_MODULE_STEM := vendor.google.wifi_ext@1.0.so
include $(BUILD_PREBUILT)