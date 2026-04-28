# AptX
PRODUCT_PROPERTY_OVERRIDES += \
    persist.bt.enableAptXHD=true \
    persist.service.btui.use_aptx=1 \
    persist.vendor.bt.a2dp_offload_cap=sbc-aptx-aptxtws-aptxhd-aac-ldac \
    persist.vendor.btstack.a2dp_offload_cap=sbc-aptx-aptxtws-aptxhd-aac-ldacs

# BT
PRODUCT_PROPERTY_OVERRIDES += \
    ro.bt.bdaddr_path=/system/vendor/etc/mocha_btmacaddr.txt

# Didim
PRODUCT_PROPERTY_OVERRIDES += \
    persist.tegra.didim.enable=1 \
    persist.tegra.didim.video=5 \
    persist.tegra.didim.normal=3

# Graphics
PRODUCT_PROPERTY_OVERRIDES += \
    ro.sf.lcd_density=320 \
    ro.opengles.version=196609 \
    persist.tegra.compositor=surfaceflinger \
    ro.zygote.disable_gl_preload=true \
    ro.sf.disable_triple_buffer=true \
    persist.sys.ui.hw=true \
    debug.egl.hw=1 \
    debug.sf.hw=1
	
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
    persist.tegra.nvmmlite=1

# OMX
PRODUCT_PROPERTY_OVERRIDES += \
    persist.media.treble_omx=false \
    media.stagefright.less-secure=true \
    media.stagefright.legacyencoder=true

# pbc 
PRODUCT_PROPERTY_OVERRIDES += \
    pbc.enabled=0 \
    pbc.log=0 \
    pbc.board_power_threshold=20000 \
    pbc.low_polling_freq_threshold=1000 \
    pbc.rails=cpu,core,dram,gp \
    pbc.cpu.power=/sys/bus/i2c/devices/7-0045/power1_input\
    pbc.cpu.cap=/dev/cpu_freq_max\
    pbc.cpu.cap.af=/sys/devices/system/cpu/cpu0/cpufreq/scaling_available_frequencies\
    pbc.core.power=/sys/bus/i2c/devices/7-0043/power1_input\
    pbc.dram.power=/sys/bus/i2c/devices/7-0049/power1_input\
    pbc.gpu.power=/sys/bus/i2c/devices/7-004b/power1_input\
    pbc.gpu.cap=/dev/gpu_freq_max\
    pbc.gpu.cap.af=/sys/devices/platform/host1x/gk20a.0/devfreq/gk20a.0/available_frequencies 

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
    persist.wlan.ti.calibrated=0 \
    persist.debug.wfd.enable=1
