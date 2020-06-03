LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := thermsys

LOCAL_SRC_FILES := thermal_sysfsread.c

include $(BUILD_EXECUTABLE)
