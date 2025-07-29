LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := libshim_atomic.cpp
LOCAL_MODULE := libshim_atomic
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
include $(BUILD_SHARED_LIBRARY)

#include $(CLEAR_VARS)
#LOCAL_SRC_FILES := libshim_audio.cpp
#LOCAL_SHARED_LIBRARIES := libaudioclient libaudioprocessing
#LOCAL_MODULE := libshim_audio
#include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := libshim_binder.cpp binder/BnServiceManager.cpp
LOCAL_SHARED_LIBRARIES := libbinder libutils
LOCAL_MODULE := libshim_binder
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := log2_kernel.c
LOCAL_MODULE := libshim_bionic
include $(BUILD_SHARED_LIBRARY)

#include $(CLEAR_VARS)
#LOCAL_SRC_FILES := libshim_gui.cpp
#LOCAL_SHARED_LIBRARIES := libgui libutils
#LOCAL_MODULE := libshim_gui
#include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := libshim_icuuc.cpp
LOCAL_SHARED_LIBRARIES := libicuuc libicui18n
LOCAL_MODULE := libshim_icuuc
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := libshim_stagefright.cpp
LOCAL_SHARED_LIBRARIES := libstagefright libstagefright_foundation libmedia
LOCAL_MODULE := libshim_stagefright
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := libshim_ui.cpp
LOCAL_SHARED_LIBRARIES := libui libgui libutils libcutils libstagefright_foundation
LOCAL_C_INCLUDES += framework/native/include frameworks/av/include
LOCAL_CFLAGS += -Wno-unused-private-field
LOCAL_MODULE := libshim_ui
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := libshim_utils.cpp
LOCAL_SHARED_LIBRARIES := libutils
LOCAL_MODULE := libshim_utils
include $(BUILD_SHARED_LIBRARY)

## liblog
include $(CLEAR_VARS)
LOCAL_SRC_FILES := stdio_vsnprintf.cpp
LOCAL_C_INCLUDES := bionic/libc/stdio
LOCAL_SHARED_LIBRARIES := liblog
LOCAL_MODULE := libs
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := zygote_whitelist.cpp
LOCAL_C_INCLUDES := frameworks/base/core/jni \
                    system/core/base/include
ifneq ($(ZYGOTE_WHITELIST_PATH_EXTRA),)
    LOCAL_CFLAGS += -DPATH_WHITELIST_EXTRA=$(ZYGOTE_WHITELIST_PATH_EXTRA)
endif
LOCAL_SHARED_LIBRARIES := liblog
LOCAL_MODULE := libshim_zw
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)
