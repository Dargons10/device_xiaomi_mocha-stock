#!/system/bin/sh

# Enable camera components:
# 1. Camera2 AOSP app (if installed)
# 2. OpenCamera (our default camera app, if installed)

# Camera2 AOSP
PKG="com.android.camera2"
pm disable --user 0 "${PKG}/com.android.camera.DisableCameraReceiver" >/dev/null 2>&1
pm enable --user 0 "${PKG}/com.android.camera.CameraLauncher" >/dev/null 2>&1
pm enable --user 0 "${PKG}/com.android.camera.CameraActivity" >/dev/null 2>&1

# OpenCamera (default)
PKG_OC="net.sourceforge.opencamera"
pm enable --user 0 "${PKG_OC}/net.sourceforge.opencamera.MainActivity" >/dev/null 2>&1
