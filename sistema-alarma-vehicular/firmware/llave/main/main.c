#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_sleep.h"

#include "app_llave.h"
#include "protocolo_seguro.h"

static const char *TAG = "MAIN_LLAVE";

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Sistema de Alarma Vehicular - LLAVE  ");
    ESP_LOGI(TAG, "  ESP32-S3 v%s                         ", esp_get_idf_version());
    ESP_LOGI(TAG, "========================================");

    // Verificar razón del wake up
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    switch(wakeup_reason) {
        case ESP_SLEEP_WAKEUP_EXT0:
            ESP_LOGI(TAG, "Wake up por botón");
            break;
        case ESP_SLEEP_WAKEUP_EXT1:
            ESP_LOGI(TAG, "Wake up por LoRa");
            break;
        case ESP_SLEEP_WAKEUP_TIMER:
            ESP_LOGI(TAG, "Wake up por timer");
            break;
        case ESP_SLEEP_WAKEUP_TOUCHPAD:
            ESP_LOGI(TAG, "Wake up por touchpad");
            break;
        case ESP_SLEEP_WAKEUP_ULP:
            ESP_LOGI(TAG, "Wake up por ULP");
            break;
        default:
            ESP_LOGI(TAG, "Arranque en frío: %d", wakeup_reason);
            break;
    }

    // Inicialización básica del sistema
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Inicializar WiFi (necesario para ESP-NOW aunque no se conecte)
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Verificar integridad del firmware
    ESP_LOGI(TAG, "Verificando integridad del sistema...");
    
    // Inicializar protocolo seguro
    ret = protocolo_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error inicializando protocolo: %s", esp_err_to_name(ret));
        esp_restart();
    }

    // Inicializar aplicación principal
    ESP_LOGI(TAG, "Inicializando aplicación llave...");
    ret = app_llave_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error inicializando app llave: %s", esp_err_to_name(ret));
        esp_restart();
    }

    // Arrancar tareas principales
    ESP_LOGI(TAG, "Arrancando tareas del sistema...");
    ret = app_llave_start_tasks();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error arrancando tareas: %s", esp_err_to_name(ret));
        esp_restart();
    }

    ESP_LOGI(TAG, "Sistema llave iniciado correctamente");
    ESP_LOGI(TAG, "Memoria libre: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "PSRAM libre: %lu bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    // Entrar en bucle principal
    app_llave_main_loop();
}
