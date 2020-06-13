# HIDL
DEVICE_FRAMEWORK_MANIFEST_FILE += $(DEVICE_PATH)/wifi-ext/manifest.xml

# Sepolicy
BOARD_PLAT_PRIVATE_SEPOLICY_DIR += \
    $(DEVICE_PATH)/wifi-ext/sepolicy \
    device/custom/sepolicy/wifi-ext/common \
    device/custom/sepolicy/wifi-ext/google