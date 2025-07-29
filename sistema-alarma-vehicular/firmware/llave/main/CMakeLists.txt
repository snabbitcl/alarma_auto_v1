idf_component_register(
    SRCS 
        "main.c" 
        "app_llave.c"
    
    INCLUDE_DIRS 
        "."
    
    REQUIRES
        driver
        esp_timer
        esp_wifi
        nvs_flash
        esp_netif
        freertos
        compartido
        esp_adc
)
