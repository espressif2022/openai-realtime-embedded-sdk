
idf_component_register(SRC_DIRS "./"
                       INCLUDE_DIRS "./"
                       REQUIRES unity esp_hw_support fatfs vfs sdmmc driver esp_wifi
                                nvs_flash esp_audio_simple_player test_utils
                                esp_codec_dev system_common
                       WHOLE_ARCHIVE)
