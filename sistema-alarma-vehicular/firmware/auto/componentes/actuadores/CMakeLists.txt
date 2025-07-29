idf_component_register(
    SRCS "vehiculo_actuadores.c"
    INCLUDE_DIRS "."
    REQUIRES "driver"
             "esp_timer"
             "esp_common"
             "log"
             "freertos"
)
