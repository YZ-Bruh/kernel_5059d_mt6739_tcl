#############################################################################
## Standard rules for installing runtime resouce overlay APKs.
##
## Set LOCAL_RRO_THEME to the theme name if the package should apply only to
## a particular theme as set by ro.boot.vendor.overlay.theme system property.
##
## If LOCAL_RRO_THEME is not set, the package will apply always, independent
## of themes.
##
#############################################################################

LOCAL_IS_RUNTIME_RESOURCE_OVERLAY := true

ifneq ($(LOCAL_SRC_FILES),)
  $(error runtime resource overlay package should not contain sources)
endif

## Modified by shuang.zhong for comile system begin
ifeq ($(LOCAL_RRO_THEME),)
  LOCAL_MODULE_PATH := $(TARGET_OUT)/overlay
else
  LOCAL_MODULE_PATH := $(TARGET_OUT)/overlay/$(LOCAL_RRO_THEME)
endif
## Modified by shuang.zhong for comile system end

include $(BUILD_SYSTEM)/package.mk

