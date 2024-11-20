# Didim
PRODUCT_PROPERTY_OVERRIDES += \
persist.tegra.didim.enable = 1 \
persist.tegra.didim.video = 5 \
persist.tegra.didim.normal = 3 

# Display
PRODUCT_PROPERTY_OVERRIDES += \
	ro.sf.lcd_density=320

# Graphics
PRODUCT_PROPERTY_OVERRIDES += \
    ro.opengles.version = 196609 \
	persist.tegra.compositor=surfaceflinger \
    ro.zygote.disable_gl_preload=true \
    ro.sf.disable_triple_buffer=true \
	debug.sf.disable_backpressure=0 \
	ro.input.noresample=1 \
	ro.com.google.clientidbase=android-nvidia

# Input
PRODUCT_PROPERTY_OVERRIDES += \
	ro.input.noresample=1

# Nvmm
PRODUCT_PROPERTY_OVERRIDES += \
	persist.tegra.nvmmlite = 1

# OMX
PRODUCT_PROPERTY_OVERRIDES += \
    persist.media.treble_omx=false \
    media.stagefright.less-secure=true \
	media.stagefright.legacyencoder=true

# pbc 
PRODUCT_PROPERTY_OVERRIDES += \
	pbc.enabled=0 \
	pbc.log=0  \
	pbc.board_power_threshold=20000 \
	pbc.low_polling_freq_threshold=1000 \
	pbc.rails=cpu,core,dram,gpu \
	pbc.cpu.power=/sys/bus/i2c/devices/7-0045/power1_input \
	pbc.cpu.cap=/dev/cpu_freq_max \
	pbc.cpu.cap.af=/sys/devices/system/cpu/cpu0/cpufreq/scaling_available_frequencies \
	pbc.core.power=/sys/bus/i2c/devices/7-0043/power1_input \
	pbc.dram.power=/sys/bus/i2c/devices/7-0049/power1_input \
	pbc.gpu.power=/sys/bus/i2c/devices/7-004b/power1_input \
	pbc.gpu.cap=/dev/gpu_freq_max \
	pbc.gpu.cap.af=/sys/devices/platform/host1x/gk20a.0/devfreq/gk20a.0/available_frequencies 

# Radio
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
	persist.wlan.ti.calibrated = 0