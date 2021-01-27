LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := avcodec
ifeq ($(TARGET_ARCH_ABI), armeabi-v7a)
   LOCAL_SRC_FILES := ../prebuilt/$(TARGET_ARCH_ABI)/libavcodec_neon.so
else
   LOCAL_SRC_FILES := ../prebuilt/$(TARGET_ARCH_ABI)/libavcodec.so
endif
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := avdevice
ifeq ($(TARGET_ARCH_ABI), armeabi-v7a)
   LOCAL_SRC_FILES := ../prebuilt/$(TARGET_ARCH_ABI)/libavdevice_neon.so
else
   LOCAL_SRC_FILES := ../prebuilt/$(TARGET_ARCH_ABI)/libavdevice.so
endif
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := avfilter
ifeq ($(TARGET_ARCH_ABI), armeabi-v7a)
   LOCAL_SRC_FILES := ../prebuilt/$(TARGET_ARCH_ABI)/libavfilter_neon.so
else
   LOCAL_SRC_FILES := ../prebuilt/$(TARGET_ARCH_ABI)/libavfilter.so
endif
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := avformat
ifeq ($(TARGET_ARCH_ABI), armeabi-v7a)
   LOCAL_SRC_FILES := ../prebuilt/$(TARGET_ARCH_ABI)/libavformat_neon.so
else
   LOCAL_SRC_FILES := ../prebuilt/$(TARGET_ARCH_ABI)/libavformat.so
endif
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := avutil
ifeq ($(TARGET_ARCH_ABI), armeabi-v7a)
   LOCAL_SRC_FILES := ../prebuilt/$(TARGET_ARCH_ABI)/libavutil_neon.so
else
   LOCAL_SRC_FILES := ../prebuilt/$(TARGET_ARCH_ABI)/libavutil.so
endif
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := swscale
ifeq ($(TARGET_ARCH_ABI), armeabi-v7a)
   LOCAL_SRC_FILES := ../prebuilt/$(TARGET_ARCH_ABI)/libswscale_neon.so
else
   LOCAL_SRC_FILES := ../prebuilt/$(TARGET_ARCH_ABI)/libswscale.so
endif
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := swresample
ifeq ($(TARGET_ARCH_ABI), armeabi-v7a)
   LOCAL_SRC_FILES := ../prebuilt/$(TARGET_ARCH_ABI)/libswresample_neon.so
else
   LOCAL_SRC_FILES := ../prebuilt/$(TARGET_ARCH_ABI)/libswresample.so
endif
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := c++_shared
LOCAL_SRC_FILES := ../prebuilt/$(TARGET_ARCH_ABI)/libc++_shared.so
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := godot-videodecoder
LOCAL_SRC_FILES := ../src/gdnative_videodecoder.c
LOCAL_EXPORT_C_INCLUDE_DIRS := ../godot_include godot_include ../ffmpeg_include ffmpeg_include
LOCAL_C_INCLUDES := ../godot_include godot_include ../ffmpeg_include ffmpeg_include
LOCAL_ALLOW_UNDEFINED_SYMBOLS := true
LOCAL_SHARED_LIBRARIES := avcodec avdevice avfilter avformat avutil swscale swresample
ifeq ($(APP_STL), c++_shared)
    LOCAL_SHARED_LIBRARIES += c++_shared # otherwise NDK will not add the library for packaging
endif
include $(BUILD_SHARED_LIBRARY)