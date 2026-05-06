LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := imx179_primax.json
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH := $(TARGET_OUT_VENDOR_ETC)/camera/tuning
LOCAL_SRC_FILES := imx179_primax.json
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := ov5693_sunny.json
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH := $(TARGET_OUT_VENDOR_ETC)/camera/tuning
LOCAL_SRC_FILES := ov5693_sunny.json
include $(BUILD_PREBUILT)
