#
# Copyright (C) 2014 The CyanogenMod Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
$(call inherit-product, frameworks/native/build/phone-xxhdpi-2048-dalvik-heap.mk)
$(call inherit-product-if-exists, vendor/xiaomi/mocha/mocha-vendor.mk)
$(call inherit-product-if-exists, vendor/xiaomi/mocha/consolemode-blobs.mk)

#AAPT_CONFIG
PRODUCT_AAPT_CONFIG += xlarge large
TARGET_SCREEN_HEIGHT := 2048
TARGET_SCREEN_WIDTH := 1536
TARGET_TEGRA_VERSION := t124

# Audio
PRODUCT_COPY_FILES += \
    device/xiaomi/mocha/audio/audio_policy.conf:system/etc/audio_policy.conf \
    device/xiaomi/mocha/audio/audio.mocha.xml:system/etc/audio.mocha.xml \
    device/xiaomi/mocha/audio/audio_effects.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_effects.xml

PRODUCT_PACKAGES += \
    android.hardware.audio@2.0-impl \
    android.hardware.audio@2.0-service \
    android.hardware.audio.effect@2.0-impl \
    audio.a2dp.default \
    audio.usb.default \
    audio.r_submix.default \
    audio.primary.tegra \
    libaudiohalcm \
    libaudio-resampler \
    libaudiospdif \
    libstagefrighthw \
    libtinycompress \
    tinycap_mocha \
    tinymix_mocha \
    tinyplay_mocha \
    libtinyalsa_mocha \
    libtinyalsa \
    xaplay

# Bluetooth
PRODUCT_COPY_FILES += \
    device/xiaomi/mocha/bluetooth/bt_vendor.conf:system/etc/bluetooth/bt_vendor.conf \
    device/xiaomi/mocha/rootdir/etc/init.btloader.sh:system/bin/init.btloader.sh

PRODUCT_PACKAGES += \
    libbt-vendor

PRODUCT_PACKAGES += \
    android.hardware.bluetooth@1.0-impl \
    android.hardware.bluetooth@1.0-service \
    
# Camera
PRODUCT_COPY_FILES += \
    device/xiaomi/mocha/camera/nvcamera.conf:system/etc/nvcamera.conf \
    device/xiaomi/mocha/camera/model_frontal.xml:system/etc/model_frontal.xml

PRODUCT_PACKAGES += \
    android.hardware.camera.provicer@2.4-impl \
    camera.device@1.0-impl \
    camera.tegra \
    libmocha_camera \
    libmocha_omx \
    libpowerservice_client \
    libmocha_libc \
    Snap

# Comm Permissions
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/handheld_core_hardware.xml:system/etc/permissions/handheld_core_hardware.xml \
    frameworks/native/data/etc/android.hardware.sensor.proximity.xml:system/etc/permissions/android.hardware.sensor.proximity.xml \
    frameworks/native/data/etc/android.software.sip.xml:system/etc/permissions/android.software.sip.xml \
    frameworks/native/data/etc/android.software.sip.voip.xml:system/etc/permissions/android.software.sip.voip.xml

# Configstore HAL
PRODUCT_PACKAGES += \
    android.hardware.configstore@1.0-impl \
    android.hardware.configstore@1.0-service


# Dexpreopt
PRODUCT_DEXPREOPT_SPEED_APPS += SystemUI

# Filesystem management tools
PRODUCT_PACKAGES += \
    setup_fs

# FM
PRODUCT_PACKAGES += \
    FMRadio \
    brcm-uim-sysfs \
    libfmjni

# Gatekeeper
PRODUCT_PACKAGES += \
    android.hardware.gatekeeper@1.0-impl \
    gatekeeper.tegra

# Grapchis
PRODUCT_PACKAGES += \
   android.hardware.graphics.allocator@2.0-impl \
   android.hardware.graphics.allocator@2.0-service \
   android.hardware.graphics.mapper@2.0-impl \
   android.hardware.renderscript@1.0-impl \
   libs \
   libshim_wvm

# Health HAL
PRODUCT_PACKAGES += \
    android.hardware.health@1.0-impl \
    android.hardware.health@1.0-service

# HIDL (needed for shield 8.0 based hidl hals)
PRODUCT_PACKAGES += \
     android.hidl.base@1.0 \
     android.hidl.base@1.0_system \
     android.hidl.manager@1.0 \
     android.hidl.manager@1.0-java

# Keymaster
PRODUCT_PACKAGES += \
    android.hardware.keymaster@3.0-impl \
    android.hardware.keymaster@3.0-service

# keylayout
PRODUCT_COPY_FILES += \
    device/xiaomi/mocha/keylayout/tegra-kbc.kl:system/usr/keylayout/tegra-kbc.kl \
    device/xiaomi/mocha/keylayout/gpio-keys.kl:system/usr/keylayout/gpio-keys.kl \
    device/xiaomi/mocha/keylayout/Vendor_0955_Product_7210.kl:system/usr/keylayout/Vendor_0955_Product_7210.kl

# Light
PRODUCT_PACKAGES += \
    android.hardware.light@2.0-service.mocha 
        
# Manifest HIDL
PRODUCT_COPY_FILES += \
    device/xiaomi/mocha/manifest.xml:system/vendor/manifest.xml

# Media config
PRODUCT_COPY_FILES += \
    frameworks/av/media/libstagefright/data/media_codecs_google_audio.xml:system/etc/media_codecs_google_audio.xml \
    frameworks/av/media/libstagefright/data/media_codecs_google_video.xml:system/etc/media_codecs_google_video.xml \
    device/xiaomi/mocha/media/media_codecs.xml:system/etc/media_codecs.xml \
    device/xiaomi/mocha/media/media_profiles.xml:system/etc/media_profiles.xml \
    device/xiaomi/mocha/media/media_codecs_performance.xml:system/etc/media_codecs_performance.xml
PRODUCT_PACKAGES += \
    android.hardware.media.omx@1.0-impl \
    android.hardware.media.omx@1.0-service


# Memory Optimizations
PRODUCT_PROPERTY_OVERRIDES += \
     ro.vendor.qti.am.reschedule_service=true \
     ro.vendor.qti.sys.fw.use_trim_settings=true \
     ro.vendor.qti.sys.fw.trim_empty_percent=50 \
     ro.vendor.qti.sys.fw.trim_cache_percent=100 \
     ro.vendor.qti.sys.fw.empty_app_percent=25

# NVIDIA
PRODUCT_COPY_FILES += \
    device/xiaomi/mocha/permissions/com.nvidia.blakemanager.xml:system/etc/permissions/com.nvidia.blakemanager.xml \
    device/xiaomi/mocha/permissions/com.nvidia.feature.xml:system/etc/permissions/com.nvidia.feature.xml \
    device/xiaomi/mocha/permissions/com.nvidia.nvsi.xml:system/etc/permissions/com.nvidia.nvsi.xml

# Overlay
DEVICE_PACKAGE_OVERLAYS += \
    device/xiaomi/mocha/overlay

# Permissions
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.bluetooth_le.xml:system/etc/permissions/android.hardware.bluetooth_le.xml \
    frameworks/native/data/etc/android.hardware.camera.autofocus.xml:system/etc/permissions/android.hardware.camera.autofocus.xml \
    frameworks/native/data/etc/android.hardware.camera.front.xml:system/etc/permissions/android.hardware.camera.front.xml \
    frameworks/native/data/etc/android.hardware.camera.full.xml:system/etc/permissions/android.hardware.camera.full.xml \
    frameworks/native/data/etc/android.hardware.camera.raw.xml:system/etc/permissions/android.hardware.camera.raw.xml \
    frameworks/native/data/etc/android.hardware.ethernet.xml:system/etc/permissions/android.hardware.ethernet.xml \
    frameworks/native/data/etc/android.hardware.location.gps.xml:system/etc/permissions/android.hardware.location.gps.xml \
    frameworks/native/data/etc/android.hardware.opengles.aep.xml:system/etc/permissions/android.hardware.opengles.aep.xml \
    frameworks/native/data/etc/android.hardware.sensor.accelerometer.xml:system/etc/permissions/android.hardware.sensor.accelerometer.xml \
    frameworks/native/data/etc/android.hardware.sensor.compass.xml:system/etc/permissions/android.hardware.sensor.compass.xml \
    frameworks/native/data/etc/android.hardware.sensor.gyroscope.xml:system/etc/permissions/android.hardware.sensor.gyroscope.xml \
    frameworks/native/data/etc/android.hardware.sensor.light.xml:system/etc/permissions/android.hardware.sensor.light.xml \
    frameworks/native/data/etc/android.hardware.touchscreen.multitouch.jazzhand.xml:system/etc/permissions/android.hardware.touchscreen.multitouch.jazzhand.xml \
    frameworks/native/data/etc/android.hardware.usb.accessory.xml:system/etc/permissions/android.hardware.usb.accessory.xml \
    frameworks/native/data/etc/android.hardware.usb.host.xml:system/etc/permissions/android.hardware.usb.host.xml \
    frameworks/native/data/etc/android.hardware.wifi.direct.xml:system/etc/permissions/android.hardware.wifi.direct.xml \
    frameworks/native/data/etc/android.hardware.wifi.xml:system/etc/permissions/android.hardware.wifi.xml \
    frameworks/native/data/etc/tablet_core_hardware.xml:system/etc/permissions/tablet_core_hardware.xml \
    frameworks/native/data/etc/android.hardware.sensor.stepcounter.xml:system/etc/permissions/android.hardware.sensor.stepcounter.xml \
    frameworks/native/data/etc/android.hardware.sensor.stepdetector.xml:system/etc/permissions/android.hardware.sensor.stepdetector.xml


# Power
PRODUCT_PACKAGES += \
    android.hardware.power@1.0-service.mocha \
    power.tegra

# Product
PRODUCT_CHARACTERISTICS := tablet

# Ramdisk
PRODUCT_PACKAGES += \
    fstab.tn8 \
    init.cal.rc \
    init.comms.rc \
    init.icera.rc \
    init.hdcp.rc \
    init.ray_touch.rc \
    init.t124.rc \
    init.tegra.rc \
    init.tlk.rc \
    init.tn8.rc \
    init.tn8.usb.rc \
    init.tn8_common.rc \
    init.ussrd.rc \
    power.tn8.rc \
    ueventd.tn8.rc \
    /etc/ussr_setup


# Seccomp
PRODUCT_COPY_FILES += \
    device/xiaomi/mocha/seccomp/mediacodec.policy:$(TARGET_COPY_OUT_VENDOR)/etc/seccomp_policy/mediacodec.policy \
    device/xiaomi/mocha/seccomp/mediaextractor.policy:$(TARGET_COPY_OUT_VENDOR)/etc/seccomp_policy/mediaextractor.policy

# Sensors
PRODUCT_PACKAGES += \
    android.hardware.sensors@1.0-impl \
	android.hardware.sensors@1.0-service \
    sensors.tegra

PRODUCT_COPY_FILES += \
    device/xiaomi/mocha/sensors/etc/hals.conf:system/etc/sensors/hals.conf

# Shims
PRODUCT_PACKAGES += \
    libshim_atomic \
    libshim_audio \
    libshim_binder \
    libshim_bionic \
    libshim_gui \
    libshim_icuuc \
    libshim_stagefright \
    libshim_ui \
    libshim_utils

#STLPORT
PRODUCT_COPY_FILES += \
    prebuilts/ndk/current/sources/cxx-stl/stlport/libs/armeabi-v7a/libstlport_shared.so:system/lib/libstlport.so

# System Properties
-include device/xiaomi/mocha/system_prop.mk

# Thermal
PRODUCT_PACKAGES += \
    android.hardware.thermal@1.0-impl \
    thermal.tn8

# Vibrator
PRODUCT_PACKAGES += \
    android.hardware.vibrator@1.0-service.mocha

# USB HAL
PRODUCT_PACKAGES += \
    android.hardware.usb@1.0-service

# Wifi
PRODUCT_COPY_FILES += \
    device/xiaomi/mocha/wifi/dhcpcd.conf:system/etc/dhcpcd/dhcpcd.conf 

# Wifi
# All Shield devices xurrently use broadcom wifi / bluetooth modules
$(call inherit-product-if-exists, hardware/broadcom/wlan/bcmdhd/config/config-bcm.mk)
PRODUCT_PACKAGES += \
    android.hardware.wifi@1.0-service \
    wificond \
    hostapd \
    conn_init \
    wpa_supplicant \
    wpa_supplicant.conf
