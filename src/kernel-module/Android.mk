# Call platform config script here

LOCAL_SAT_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := sat
LOCAL_MODULE_PATH := $(LOCAL_SAT_PATH)

include $(BUILD_EXTERNAL_KERNEL_MODULE)
