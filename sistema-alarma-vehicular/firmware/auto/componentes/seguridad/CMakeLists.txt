idf_component_register(
    SRCS "vehiculo_seguridad.c"
    INCLUDE_DIRS "."
    REQUIRES "esp_timer"
             "esp_system"
             "esp_common"
             "log"
             "freertos"
)
