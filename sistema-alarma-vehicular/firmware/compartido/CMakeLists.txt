idf_component_register(
    SRCS 
        "src/protocolo_seguro.c"
        "src/rolling_code.c"
        "src/cripto_utils.c"
        "src/config_au915.c"
    
    INCLUDE_DIRS 
        "include"
    
    REQUIRES
        driver
        esp_hw_support
        mbedtls
        esp_timer
        nvs_flash
        bootloader_support
        efuse
)
