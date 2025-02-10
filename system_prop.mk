# BT
PRODUCT_PROPERTY_OVERRIDES += \
ro.bt.bdaddr_path=/system/vendor/etc/mocha_btmacaddr.txt

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

# LMKD options
PRODUCT_PROPERTY_OVERRIDES += \
 	ro.lmk.low=1001 \
 	ro.lmk.medium=800 \
 	ro.lmk.critical=0 \
 	ro.lmk.critical_upgrade=false \
 	ro.lmk.upgrade_pressure=100 \
 	ro.lmk.downgrade_pressure=100 \
 	ro.lmk.kill_heaviest_task=true \
 	ro.lmk.kill_timeout_ms=100

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
