# #######################################
# # target static library
# include $(CLEAR_VARS)
# LOCAL_SHARED_LIBRARIES := $(log_shared_libraries)libcutils
# 
# # The static library should be used in only unbundled apps
# # and we don't have clang in unbundled build yet.
# 
# LOCAL_MODULE_TAGS := optional
# LOCAL_MODULE := libcrypto_static-ss
# LOCAL_ADDITIONAL_DEPENDENCIES := $(LOCAL_PATH)/android-config.mk $(LOCAL_PATH)/Crypto.mk
# 
# include $(LOCAL_PATH)/Crypto-config-target.mk
# include $(LOCAL_PATH)/android-config.mk
# 
# # Replace cflags with static-specific cflags so we dont build in libdl deps
# LOCAL_CFLAGS_32 := $(openssl_cflags_static_32)
# LOCAL_CFLAGS_64 := $(openssl_cflags_static_64)
# include $(BUILD_STATIC_LIBRARY)
# 
#######################################
# target shared library
include $(CLEAR_VARS)
LOCAL_SHARED_LIBRARIES := $(log_shared_libraries) libcutils

# If we're building an unbundled build, don't try to use clang since it's not
# in the NDK yet. This can be removed when a clang version that is fast enough
# in the NDK.


LOCAL_ASFLAGS += -no-integrated-as
LOCAL_CFLAGS += -no-integrated-as
ifeq (,$(TARGET_BUILD_APPS))

ifeq ($(HOST_OS), darwin)
LOCAL_ASFLAGS += -no-integrated-as
LOCAL_CFLAGS += -no-integrated-as
endif

endif
LOCAL_LDFLAGS += -llog -ldl
ifeq ($(TARGET_2ND_ARCH),arm)
LOCAL_LDFLAGS_$(TARGET_2ND_ARCH) += -fPIC
LOCAL_CFLAGS_$(TARGET_2ND_ARCH) += -fPIC
LOCAL_ASFLAGS_$(TARGET_2ND_ARCH) += -fPIC
endif
LOCAL_MODULE_TAGS := optional
LOCAL_PROPRIETARY_MODULE := true
LOCAL_MODULE_OWNER := mtk
LOCAL_MODULE := libcrypto-ss
LOCAL_ADDITIONAL_DEPENDENCIES := $(LOCAL_PATH)/android-config.mk $(LOCAL_PATH)/Crypto.mk
LOCAL_CLANG_ASFLAGS_arm += -no-integrated-as
LOCAL_CLANG_ASFLAGS_arm64 += -march=armv8-a+crypto
include $(LOCAL_PATH)/Crypto-config-target.mk
include $(LOCAL_PATH)/android-config.mk

ifdef MTK_SHARED_LIBRARY
	include $(MTK_SHARED_LIBRARY)
else
	include $(BUILD_SHARED_LIBRARY)
endif
