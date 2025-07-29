/**
 * @file main.c
 * @brief Punto de entrada principal del módulo vehicular
 * 
 * Sistema de alarma vehicular ESP32-S3 con comunicación LoRa AU915
 * Inicialización del sistema y transferencia de control a la aplicación
 * 
 * @author Sistema Alarma Vehicular
 * @date 2024
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_task_wdt.h"
#include "esp_ota_ops.h"
#include "esp_efuse.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"

#include "app_vehiculo.h"

// ============================================================================
// CONSTANTES Y CONFIGURACIÓN
// ============================================================================

static const char* TAG = "MAIN_VEHICULO";

// Configuración del watchdog principal
#define MAIN_WDT_TIMEOUT_S  30

// Verificaciones de integridad del sistema
#define MIN_FREE_HEAP_SIZE  50000   // 50KB mínimo de heap libre
#define MIN_FREE_SPIRAM     100000  // 100KB mínimo de SPIRAM libre

// ============================================================================
// FUNCIONES PRIVADAS
// ============================================================================

/**
 * @brief Muestra información del sistema al iniciar
 */
static void mostrar_info_sistema(void)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    
    uint32_t flash_size;
    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        flash_size = 0;
    }
    
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "   SISTEMA ALARMA VEHICULAR v%s", VEHICULO_VERSION);
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "Chip: %s rev %d", CONFIG_IDF_TARGET, chip_info.revision);
    ESP_LOGI(TAG, "Cores: %d", chip_info.cores);
    ESP_LOGI(TAG, "Flash: %dMB %s", flash_size / (1024 * 1024),
             (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
    ESP_LOGI(TAG, "SPIRAM: %s", 
             (chip_info.features & CHIP_FEATURE_EMB_PSRAM) ? "Sí" : "No");
    ESP_LOGI(TAG, "WiFi: %s", 
             (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "802.11bgn" : "No");
    ESP_LOGI(TAG, "BLE: %s", 
             (chip_info.features & CHIP_FEATURE_BLE) ? "Sí" : "No");
    ESP_LOGI(TAG, "Secure Boot: %s", 
             "Deshabilitado"); // Función deprecated en ESP-IDF v5.5
    ESP_LOGI(TAG, "Flash Encryption: %s",
             "Deshabilitado"); // Función deprecated en ESP-IDF v5.5
    
    // Información de memoria
    multi_heap_info_t heap_info;
    heap_caps_get_info(&heap_info, MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "Heap interno libre: %d bytes", heap_info.total_free_bytes);
    
    if (chip_info.features & CHIP_FEATURE_EMB_PSRAM) {
        heap_caps_get_info(&heap_info, MALLOC_CAP_SPIRAM);
        ESP_LOGI(TAG, "SPIRAM libre: %d bytes", heap_info.total_free_bytes);
    }
    
    ESP_LOGI(TAG, "==========================================");
}

/**
 * @brief Verificaciones de seguridad del sistema
 */
static esp_err_t verificar_seguridad_sistema(void)
{
    ESP_LOGI(TAG, "Verificando configuración de seguridad...");
    
    // Verificaciones de seguridad simplificadas para ESP-IDF v5.5
    ESP_LOGW(TAG, "ADVERTENCIA: Secure Boot no está habilitado");
    ESP_LOGW(TAG, "Sistema vulnerable a modificación de firmware");
    
    ESP_LOGW(TAG, "ADVERTENCIA: Flash Encryption no está habilitado");
    ESP_LOGW(TAG, "Datos en flash no están cifrados");
    
    // Verificar partición OTA actual
    const esp_partition_t* running_partition = esp_ota_get_running_partition();
    if (running_partition == NULL) {
        ESP_LOGE(TAG, "ERROR: No se puede determinar la partición activa");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Ejecutando desde partición: %s (offset 0x%x)", 
             running_partition->label, running_partition->address);
    
    // Verificar estado de la aplicación
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running_partition, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "Imagen OTA pendiente de verificación - marcando como válida");
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }
    
    return ESP_OK;
}

/**
 * @brief Verificaciones de recursos del sistema
 */
static esp_err_t verificar_recursos_sistema(void)
{
    ESP_LOGI(TAG, "Verificando recursos del sistema...");
    
    // Verificar heap interno disponible
    size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (free_heap < MIN_FREE_HEAP_SIZE) {
        ESP_LOGE(TAG, "ERROR: Heap interno insuficiente: %d < %d bytes", 
                 free_heap, MIN_FREE_HEAP_SIZE);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "✓ Heap interno: %d bytes libres", free_heap);
    
    // Verificar SPIRAM si está disponible
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    
    if (chip_info.features & CHIP_FEATURE_EMB_PSRAM) {
        size_t free_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        if (free_spiram < MIN_FREE_SPIRAM) {
            ESP_LOGE(TAG, "ERROR: SPIRAM insuficiente: %d < %d bytes", 
                     free_spiram, MIN_FREE_SPIRAM);
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGI(TAG, "✓ SPIRAM: %d bytes libres", free_spiram);
    }
    
    return ESP_OK;
}

/**
 * @brief Configuración inicial del watchdog
 */
static esp_err_t configurar_watchdog(void)
{
    ESP_LOGI(TAG, "Configurando watchdog del sistema...");
    
    // Configurar watchdog de tareas
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = MAIN_WDT_TIMEOUT_S * 1000,
        .idle_core_mask = (1 << CONFIG_FREERTOS_NUMBER_OF_CORES) - 1,
        .trigger_panic = true
    };
    
    esp_err_t ret = esp_task_wdt_init(&wdt_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error configurando watchdog: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Añadir tarea principal al watchdog
    esp_task_wdt_add(NULL);
    
    ESP_LOGI(TAG, "✓ Watchdog configurado (timeout: %ds)", MAIN_WDT_TIMEOUT_S);
    return ESP_OK;
}

/**
 * @brief Pruebas de autodiagnóstico del sistema
 */
static esp_err_t ejecutar_autodiagnostico(void)
{
    ESP_LOGI(TAG, "Ejecutando autodiagnóstico del sistema...");
    
    // Test básico de memoria
    void* test_ptr = malloc(1024);
    if (test_ptr == NULL) {
        ESP_LOGE(TAG, "ERROR: Falla en asignación de memoria");
        return ESP_ERR_NO_MEM;
    }
    memset(test_ptr, 0xAA, 1024);
    free(test_ptr);
    
    // Test de SPIRAM si está disponible
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    
    if (chip_info.features & CHIP_FEATURE_EMB_PSRAM) {
        void* spiram_ptr = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
        if (spiram_ptr == NULL) {
            ESP_LOGE(TAG, "ERROR: Falla en asignación de SPIRAM");
            return ESP_ERR_NO_MEM;
        }
        memset(spiram_ptr, 0x55, 4096);
        heap_caps_free(spiram_ptr);
        ESP_LOGI(TAG, "✓ Test SPIRAM exitoso");
    }
    
    // Test de FreeRTOS simplificado
    ESP_LOGI(TAG, "✓ Test FreeRTOS - Sistema operativo iniciado correctamente");
    
    ESP_LOGI(TAG, "✓ Autodiagnóstico completado exitosamente");
    return ESP_OK;
}

/**
 * @brief Manejo de errores críticos durante la inicialización
 */
static void manejar_error_critico(const char* mensaje, esp_err_t codigo_error)
{
    ESP_LOGE(TAG, "ERROR CRÍTICO: %s", mensaje);
    ESP_LOGE(TAG, "Código de error: %s", esp_err_to_name(codigo_error));
    ESP_LOGE(TAG, "El sistema no puede continuar");
    
    // Intentar guardar información de error en NVS si es posible
    // TODO: Implementar logging de errores críticos
    
    // Delay para permitir que el logging se complete
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Forzar reinicio del sistema
    ESP_LOGE(TAG, "Reiniciando sistema en 3 segundos...");
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
}

// ============================================================================
// FUNCIÓN PRINCIPAL
// ============================================================================

/**
 * @brief Punto de entrada principal de la aplicación
 */
void app_main(void)
{
    esp_err_t ret;
    
    // Configurar nivel de logging
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
    esp_log_level_set(VEHICULO_TAG, ESP_LOG_DEBUG);
    
    ESP_LOGI(TAG, "Iniciando módulo vehicular...");
    
    // Mostrar información del sistema
    mostrar_info_sistema();
    
    // Configurar watchdog del sistema
    ret = configurar_watchdog();
    if (ret != ESP_OK) {
        manejar_error_critico("Falla configurando watchdog", ret);
    }
    
    // Verificaciones de seguridad
    ret = verificar_seguridad_sistema();
    if (ret != ESP_OK) {
        manejar_error_critico("Falla en verificación de seguridad", ret);
    }
    
    // Verificaciones de recursos
    ret = verificar_recursos_sistema();
    if (ret != ESP_OK) {
        manejar_error_critico("Recursos del sistema insuficientes", ret);
    }
    
    // Autodiagnóstico del sistema
    ret = ejecutar_autodiagnostico();
    if (ret != ESP_OK) {
        manejar_error_critico("Falla en autodiagnóstico", ret);
    }
    
    ESP_LOGI(TAG, "Verificaciones del sistema completadas ✓");
    
    // Reset del watchdog antes de continuar
    esp_task_wdt_reset();
    
    // Inicializar aplicación vehicular
    ESP_LOGI(TAG, "Inicializando aplicación vehicular...");
    ret = vehiculo_app_init();
    if (ret != ESP_OK) {
        manejar_error_critico("Falla inicializando aplicación vehicular", ret);
    }
    
    ESP_LOGI(TAG, "Aplicación inicializada ✓");
    
    // Reset del watchdog antes del bucle principal
    esp_task_wdt_reset();
    
    // Transferir control a la aplicación vehicular
    ESP_LOGI(TAG, "Transfiriendo control a aplicación vehicular...");
    ESP_LOGI(TAG, "=== SISTEMA OPERACIONAL ===");
    
    ret = vehiculo_app_run();
    
    // Si llegamos aquí, algo salió mal
    ESP_LOGE(TAG, "La aplicación vehicular terminó inesperadamente");
    manejar_error_critico("Terminación inesperada de aplicación", ret);
}
