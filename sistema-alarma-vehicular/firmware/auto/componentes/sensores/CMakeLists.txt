idf_component_register(
    SRCS "vehiculo_sensores.c"
    INCLUDE_DIRS "."
    REQUIRES "driver"
             "esp_timer"
             "esp_adc"
             "esp_common"
             "log"
             "freertos"
)
