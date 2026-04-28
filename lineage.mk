# Inherit device configuration for mocha.
$(call inherit-product, device/xiaomi/mocha/full_mocha.mk)

# Boot Animtion
TARGET_BOOTANIMATION_HALF_RES := true

# Inherit some common lineage stuff.
$(call inherit-product, vendor/lineage/config/common_mini_tablet_wifionly.mk)

# Temporary: bypass SetupWizard until EGL stack is fully stable
PRODUCT_PACKAGES := $(filter-out LineageSetupWizard,$(PRODUCT_PACKAGES))

PRODUCT_NAME := lineage_mocha
PRODUCT_DEVICE := mocha
PRODUCT_BRAND := xiaomi
PRODUCT_MANUFACTURER := Xiaomi
BOARD_VENDOR := Xiaomi

PRODUCT_BUILD_PROP_OVERRIDES += \
    PRIVATE_BUILD_DESC="mocha-userdebug 8.1.0 OPM7.181205.001 V9.2.4.0.KXFCNEK release-keys"

BUILD_FINGERPRINT := Xiaomi/mocha/mocha:8.1.0/OPM7.181205.001/V9.2.4.0.KXFCNEK:userdebug/release-keys

PRODUCT_GMS_CLIENTID_BASE := android-xiaomi
