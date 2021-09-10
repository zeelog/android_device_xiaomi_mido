ifeq ($(TARGET_BOARD_PLATFORM),msm8937)
PRODUCT_COPY_FILES += hardware/qcom/media/conf_files/msm8937/media_profiles_8937.xml:system/etc/media_profiles.xml \
                      hardware/qcom/media/conf_files/msm8937/media_codecs_8937.xml:system/etc/media_codecs.xml \
                      hardware/qcom/media/conf_files/msm8937/media_codecs_performance_8937.xml:system/etc/media_codecs_performance.xml
endif #TARGET_BOARD_PLATFORM
