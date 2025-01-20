idf_component_register(
	SRC_DIRS "."
	INCLUDE_DIRS "."
    REQUIRES driver
        esp_wifi
        fatfs
        nvs_flash peer
        esp_psram
        esp_http_client
        system_common
)

idf_component_get_property(lib peer COMPONENT_LIB)
target_compile_options(${lib} PRIVATE -Wno-error=restrict)
target_compile_options(${lib} PRIVATE -Wno-error=stringop-truncation)
target_compile_options(${lib} PRIVATE -Wno-error=format)

idf_component_get_property(lib sepfy__srtp COMPONENT_LIB)
target_compile_options(${lib} PRIVATE -Wno-error=incompatible-pointer-types)

idf_component_get_property(lib espressif__gmf_core COMPONENT_LIB)
target_compile_options(${lib} PRIVATE -Wno-error=incompatible-pointer-types)

spiffs_create_partition_assets(
    animation
    "../wav"
    FLASH_IN_PROJECT
    MMAP_FILE_SUPPORT_FORMAT ".wav"
)
