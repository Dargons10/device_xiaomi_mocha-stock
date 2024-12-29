# BT
PRODUCT_PROPERTY_OVERRIDES += \
    ro.bt.bdaddr_path=/system/etc/bluetooth/bdaddr

# Graphics
PRODUCT_PROPERTY_OVERRIDES += \
    persist.sys.ui.hw=true \
    debug.sf.disable_backpressure=1 \
    debug.sf.latch_unsignaled=1
	
# Input
PRODUCT_PROPERTY_OVERRIDES += \
	ro.input.noresample=1

# Lineage genuine
PRODUCT_PROPERTY_OVERRIDES += \
    persist.lineage.nofool=true

# Nvmm
PRODUCT_PROPERTY_OVERRIDES += \
	persist.tegra.nvmmlite = 1

# OMX
PRODUCT_PROPERTY_OVERRIDES += \
    persist.media.treble_omx=false \
    media.stagefright.less-secure=true \
	media.stagefright.legacyencoder=true

#Radio
PRODUCT_PROPERTY_OVERRIDES += \
	ro.radio.noril=yes

#Usb
PRODUCT_PROPERTY_OVERRIDES += \
	persist.sys.usb.config=adb \
	sys.usb.config=adb \
	persist.sys.isUsbOtgEnabled=1

# Widevine drm
PRODUCT_PROPERTY_OVERRIDES += \
	drm.service.enabled=true

#Wifi
PRODUCT_PROPERTY_OVERRIDES += \
    wifi.interface=wlan0 \
	ap.interface=wlan0 \
	persist.wlan.ti.calibrated = 0 \
	persist.debug.wfd.enable=1