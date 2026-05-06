LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

# New Mocha Camera HAL (HAL3)
LOCAL_SRC_FILES := \
    MochaCameraHAL.cpp \
    CameraPipeline.cpp \
    isp/DemosaicNEON.cpp \
    isp/ColorConvNEON.cpp

LOCAL_SHARED_LIBRARIES := \
    libhardware \
    liblog \
    libutils \
    libcutils \
    libcamera_metadata

LOCAL_C_INCLUDES := \
    frameworks/native/include \
    frameworks/native/libs/nativebase/include \
    frameworks/av/include \
    system/core/include \
    system/media/camera/include \
    hardware/libhardware/include \
    device/xiaomi/mocha/camera

LOCAL_CFLAGS := -DLOG_TAG=\"MochaCameraHAL\"

LOCAL_32_BIT_ONLY := true
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_VENDOR_MODULE := true

LOCAL_MODULE := camera.$(TARGET_BOARD_PLATFORM)
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
