#!/system/bin/sh

PKG="com.android.camera2"

pm disable --user 0 "${PKG}/com.android.camera.DisableCameraReceiver" >/dev/null 2>&1

pm enable --user 0 "${PKG}/com.android.camera.CameraLauncher" >/dev/null 2>&1
pm enable --user 0 "${PKG}/com.android.camera.CameraActivity" >/dev/null 2>&1
pm enable --user 0 "${PKG}/com.android.camera.CaptureActivity" >/dev/null 2>&1
pm enable --user 0 "${PKG}/com.android.camera.SecureCameraActivity" >/dev/null 2>&1
pm enable --user 0 "${PKG}/com.android.camera.VideoCamera" >/dev/null 2>&1
