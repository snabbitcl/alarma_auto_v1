/**
 * @file vehiculo_actuadores.c
 * @brief Implementación del control de actuadores del vehículo
 * 
 * Sistema robusto de control de actuadores con protecciones de seguridad,
 * timeouts automáticos y monitoreo de integridad
 * 
 * @author Sistema Alarma Vehicular  
 * @date 2024
 */

#include "vehiculo_actuadores.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include <inttypes.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>

// ============================================================================
// VARIABLES GLOBALES Y CONFIGURACIÓN
// ============================================================================

static vehiculo_actuadores_t* g_actuadores = NULL;
static const char* TAG = ACTUADORES_TAG;

// Configuración por defecto de actuadores
static const actuador_config_t ACTUADORES_CONFIG_DEFAULT[ACTUADOR_MAX_COUNT] = {
    [ACTUADOR_SIRENA] = {
        .pin = GPIO_NUM_4,
        .activo_alto = true,
        .tiempo_max_activacion_us = TIEMPO_MAX_SIRENA_US,
        .corriente_max_ma = 2000,
        .nombre = "Sirena Principal",
        .critico_seguridad = true
    },
    [ACTUADOR_LUCES] = {
        .pin = GPIO_NUM_5,
        .activo_alto = true,
        .tiempo_max_activacion_us = TIEMPO_MAX_LUCES_US,
        .corriente_max_ma = 1500,
        .nombre = "Luces Intermitentes",
        .critico_seguridad = false
    },
    [ACTUADOR_BLOQUEO_MOTOR] = {
        .pin = GPIO_NUM_6,
        .activo_alto = true,
        .tiempo_max_activacion_us = TIEMPO_MAX_BLOQUEO_US,
        .corriente_max_ma = 500,
        .nombre = "Bloqueo Motor",
        .critico_seguridad = true
    },
    [ACTUADOR_TRACCION] = {
        .pin = GPIO_NUM_7,
        .activo_alto = true,
        .tiempo_max_activacion_us = TIEMPO_MAX_TRACCION_US,
        .corriente_max_ma = 300,
        .nombre = "Sistema Tracción",
        .critico_seguridad = true
    },
    [ACTUADOR_BUZZER] = {
        .pin = GPIO_NUM_8,
        .activo_alto = true,
        .tiempo_max_activacion_us = TIEMPO_MAX_BUZZER_US,
        .corriente_max_ma = 100,
        .nombre = "Buzzer Local",
        .critico_seguridad = false
    },
    [ACTUADOR_LED_ESTADO] = {
        .pin = GPIO_NUM_9,
        .activo_alto = true,
        .tiempo_max_activacion_us = UINT64_MAX,  // Sin límite para LED de estado
        .corriente_max_ma = 20,
        .nombre = "LED Estado",
        .critico_seguridad = false
    }
};

// ============================================================================
// FUNCIONES PRIVADAS
// ============================================================================

/**
 * @brief Callback para timeout de activación de actuador
 */
static void actuador_timeout_callback(void* arg)
{
    actuador_tipo_t tipo = (actuador_tipo_t)(uintptr_t)arg;
    
    if (tipo >= ACTUADOR_MAX_COUNT) {
        ESP_LOGE(TAG, "Tipo de actuador inválido en timeout: %d", tipo);
        return;
    }
    
    ESP_LOGW(TAG, "TIMEOUT de seguridad para actuador %s", 
             g_actuadores->config[tipo].nombre);
    
    // Forzar desactivación por seguridad
    if (xSemaphoreTake(g_actuadores->mutex_actuadores, pdMS_TO_TICKS(100)) == pdTRUE) {
        gpio_set_level(g_actuadores->config[tipo].pin, 
                      !g_actuadores->config[tipo].activo_alto);
        
        g_actuadores->runtime[tipo].activo = false;
        g_actuadores->runtime[tipo].estado = ACTUADOR_ESTADO_TIMEOUT;
        g_actuadores->runtime[tipo].conteo_errores++;
        
        xSemaphoreGive(g_actuadores->mutex_actuadores);
    }
    
    ESP_LOGE(TAG, "Actuador %s desactivado por timeout de seguridad", 
             g_actuadores->config[tipo].nombre);
}

/**
 * @brief Callback para verificación periódica del sistema
 */
static void verificacion_periodica_callback(void* arg)
{
    if (!g_actuadores || !g_actuadores->inicializado) {
        return;
    }
    
    uint64_t tiempo_actual = esp_timer_get_time();
    
    if (xSemaphoreTake(g_actuadores->mutex_actuadores, pdMS_TO_TICKS(50)) == pdTRUE) {
        
        for (int i = 0; i < ACTUADOR_MAX_COUNT; i++) {
            actuador_runtime_t* runtime = &g_actuadores->runtime[i];
            
            if (runtime->activo) {
                // Verificar si excede tiempo máximo permitido
                uint64_t tiempo_activo = tiempo_actual - runtime->tiempo_activacion_inicio;
                
                if (tiempo_activo > g_actuadores->config[i].tiempo_max_activacion_us) {
                    ESP_LOGW(TAG, "Actuador %s excede tiempo máximo (%llu us)", 
                             g_actuadores->config[i].nombre, tiempo_activo);
                    
                    // Forzar desactivación
                    gpio_set_level(g_actuadores->config[i].pin, 
                                  !g_actuadores->config[i].activo_alto);
                    runtime->activo = false;
                    runtime->estado = ACTUADOR_ESTADO_TIMEOUT;
                    runtime->conteo_errores++;
                }
            }
        }
        
        g_actuadores->timestamp_ultimo_heartbeat = tiempo_actual;
        xSemaphoreGive(g_actuadores->mutex_actuadores);
    }
}

/**
 * @brief Inicializa GPIO para un actuador específico
 */
static esp_err_t init_gpio_actuador(actuador_tipo_t tipo)
{
    if (tipo >= ACTUADOR_MAX_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << g_actuadores->config[tipo].pin),
        .pull_down_en = !g_actuadores->config[tipo].activo_alto,
        .pull_up_en = g_actuadores->config[tipo].activo_alto
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error configurando GPIO %d para %s: %s", 
                 g_actuadores->config[tipo].pin,
                 g_actuadores->config[tipo].nombre,
                 esp_err_to_name(ret));
        return ret;
    }
    
    // Inicializar en estado inactivo
    gpio_set_level(g_actuadores->config[tipo].pin, 
                  !g_actuadores->config[tipo].activo_alto);
    
    // Crear timer de timeout para este actuador
    esp_timer_create_args_t timer_args = {
        .callback = actuador_timeout_callback,
        .arg = (void*)(uintptr_t)tipo,
        .name = g_actuadores->config[tipo].nombre
    };
    
    ret = esp_timer_create(&timer_args, &g_actuadores->runtime[tipo].timer_timeout);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error creando timer para %s: %s", 
                 g_actuadores->config[tipo].nombre,
                 esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Actuador %s inicializado en GPIO %d", 
             g_actuadores->config[tipo].nombre,
             g_actuadores->config[tipo].pin);
    
    return ESP_OK;
}

/**
 * @brief Valida parámetros de entrada
 */
static bool validar_tipo_actuador(actuador_tipo_t tipo)
{
    return (tipo >= 0 && tipo < ACTUADOR_MAX_COUNT);
}

// ============================================================================
// FUNCIONES PÚBLICAS
// ============================================================================

esp_err_t vehiculo_actuadores_init(void)
{
    if (g_actuadores != NULL) {
        ESP_LOGW(TAG, "Módulo de actuadores ya inicializado");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Inicializando módulo de actuadores del vehículo");
    
    // Alocar estructura principal
    g_actuadores = calloc(1, sizeof(vehiculo_actuadores_t));
    if (!g_actuadores) {
        ESP_LOGE(TAG, "Error asignando memoria para actuadores");
        return ESP_ERR_NO_MEM;
    }
    
    // Copiar configuración por defecto
    memcpy(g_actuadores->config, ACTUADORES_CONFIG_DEFAULT, 
           sizeof(ACTUADORES_CONFIG_DEFAULT));
    
    // Crear mutex para acceso thread-safe
    g_actuadores->mutex_actuadores = xSemaphoreCreateMutex();
    if (!g_actuadores->mutex_actuadores) {
        ESP_LOGE(TAG, "Error creando mutex de actuadores");
        free(g_actuadores);
        g_actuadores = NULL;
        return ESP_ERR_NO_MEM;
    }
    
    // Inicializar estado runtime de cada actuador
    for (int i = 0; i < ACTUADOR_MAX_COUNT; i++) {
        g_actuadores->runtime[i].estado = ACTUADOR_ESTADO_INACTIVO;
        g_actuadores->runtime[i].activo = false;
        g_actuadores->runtime[i].tiempo_activacion_inicio = 0;
        g_actuadores->runtime[i].tiempo_total_activo = 0;
        g_actuadores->runtime[i].conteo_activaciones = 0;
        g_actuadores->runtime[i].conteo_errores = 0;
        g_actuadores->runtime[i].timer_timeout = NULL;
        
        // Inicializar GPIO y timer para cada actuador
        esp_err_t ret = init_gpio_actuador((actuador_tipo_t)i);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Error inicializando actuador %d", i);
            vehiculo_actuadores_deinit();
            return ret;
        }
    }
    
    // Crear timer de verificación periódica
    esp_timer_create_args_t timer_args = {
        .callback = verificacion_periodica_callback,
        .name = "actuadores_verificacion"
    };
    
    esp_err_t ret = esp_timer_create(&timer_args, &g_actuadores->timer_verificacion);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error creando timer de verificación: %s", esp_err_to_name(ret));
        vehiculo_actuadores_deinit();
        return ret;
    }
    
    // Iniciar timer de verificación periódica
    ret = esp_timer_start_periodic(g_actuadores->timer_verificacion, 
                                  INTERVALO_VERIFICACION_MS * 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error iniciando timer de verificación: %s", esp_err_to_name(ret));
        vehiculo_actuadores_deinit();
        return ret;
    }
    
    g_actuadores->inicializado = true;
    g_actuadores->timestamp_ultimo_heartbeat = esp_timer_get_time();
    
    ESP_LOGI(TAG, "Módulo de actuadores inicializado correctamente");
    return ESP_OK;
}

esp_err_t vehiculo_actuadores_deinit(void)
{
    if (!g_actuadores) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Desinicializando módulo de actuadores");
    
    // Apagar todos los actuadores
    vehiculo_actuadores_apagar_todos();
    
    // Detener timer de verificación
    if (g_actuadores->timer_verificacion) {
        esp_timer_stop(g_actuadores->timer_verificacion);
        esp_timer_delete(g_actuadores->timer_verificacion);
    }
    
    // Limpiar timers de timeout de cada actuador
    for (int i = 0; i < ACTUADOR_MAX_COUNT; i++) {
        if (g_actuadores->runtime[i].timer_timeout) {
            esp_timer_stop(g_actuadores->runtime[i].timer_timeout);
            esp_timer_delete(g_actuadores->runtime[i].timer_timeout);
        }
    }
    
    // Liberar mutex
    if (g_actuadores->mutex_actuadores) {
        vSemaphoreDelete(g_actuadores->mutex_actuadores);
    }
    
    // Liberar memoria
    free(g_actuadores);
    g_actuadores = NULL;
    
    ESP_LOGI(TAG, "Módulo de actuadores desinicializado");
    return ESP_OK;
}

esp_err_t vehiculo_actuador_activar(actuador_tipo_t tipo, uint32_t duracion_ms)
{
    if (!g_actuadores || !g_actuadores->inicializado) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!validar_tipo_actuador(tipo)) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Activando actuador %s por %d ms", 
             g_actuadores->config[tipo].nombre, duracion_ms);
    
    if (xSemaphoreTake(g_actuadores->mutex_actuadores, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Timeout obteniendo mutex para activar actuador");
        return ESP_ERR_TIMEOUT;
    }
    
    actuador_runtime_t* runtime = &g_actuadores->runtime[tipo];
    
    // Verificar si ya está activo
    if (runtime->activo) {
        ESP_LOGW(TAG, "Actuador %s ya está activo", g_actuadores->config[tipo].nombre);
        xSemaphoreGive(g_actuadores->mutex_actuadores);
        return ESP_ERR_INVALID_STATE;
    }
    
    // Verificar si está en estado de error
    if (runtime->estado == ACTUADOR_ESTADO_ERROR) {
        ESP_LOGE(TAG, "Actuador %s en estado de error", g_actuadores->config[tipo].nombre);
        xSemaphoreGive(g_actuadores->mutex_actuadores);
        return ESP_ERR_INVALID_STATE;
    }
    
    // Activar GPIO
    gpio_set_level(g_actuadores->config[tipo].pin, g_actuadores->config[tipo].activo_alto);
    
    // Actualizar estado
    runtime->activo = true;
    runtime->estado = ACTUADOR_ESTADO_ACTIVO;
    runtime->tiempo_activacion_inicio = esp_timer_get_time();
    runtime->conteo_activaciones++;
    g_actuadores->conteo_operaciones_totales++;
    
    // Configurar timeout si se especifica duración
    if (duracion_ms > 0) {
        uint64_t timeout_us = (uint64_t)duracion_ms * 1000;
        
        // Verificar que no exceda el máximo permitido
        if (timeout_us > g_actuadores->config[tipo].tiempo_max_activacion_us) {
            timeout_us = g_actuadores->config[tipo].tiempo_max_activacion_us;
            ESP_LOGW(TAG, "Duración limitada a %llu us para %s por seguridad", 
                     timeout_us, g_actuadores->config[tipo].nombre);
        }
        
        esp_err_t ret = esp_timer_start_once(runtime->timer_timeout, timeout_us);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Error iniciando timer de timeout: %s", esp_err_to_name(ret));
        }
    }
    
    xSemaphoreGive(g_actuadores->mutex_actuadores);
    
    ESP_LOGI(TAG, "Actuador %s activado correctamente", g_actuadores->config[tipo].nombre);
    return ESP_OK;
}

esp_err_t vehiculo_actuador_desactivar(actuador_tipo_t tipo)
{
    if (!g_actuadores || !g_actuadores->inicializado) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!validar_tipo_actuador(tipo)) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Desactivando actuador %s", g_actuadores->config[tipo].nombre);
    
    if (xSemaphoreTake(g_actuadores->mutex_actuadores, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Timeout obteniendo mutex para desactivar actuador");
        return ESP_ERR_TIMEOUT;
    }
    
    actuador_runtime_t* runtime = &g_actuadores->runtime[tipo];
    
    // Desactivar GPIO
    gpio_set_level(g_actuadores->config[tipo].pin, !g_actuadores->config[tipo].activo_alto);
    
    // Detener timer de timeout si está activo
    esp_timer_stop(runtime->timer_timeout);
    
    // Actualizar estadísticas si estaba activo
    if (runtime->activo) {
        uint64_t tiempo_activo = esp_timer_get_time() - runtime->tiempo_activacion_inicio;
        runtime->tiempo_total_activo += tiempo_activo;
    }
    
    // Actualizar estado
    runtime->activo = false;
    runtime->estado = ACTUADOR_ESTADO_INACTIVO;
    runtime->tiempo_activacion_inicio = 0;
    
    xSemaphoreGive(g_actuadores->mutex_actuadores);
    
    ESP_LOGI(TAG, "Actuador %s desactivado correctamente", g_actuadores->config[tipo].nombre);
    return ESP_OK;
}

esp_err_t vehiculo_actuadores_apagar_todos(void)
{
    if (!g_actuadores || !g_actuadores->inicializado) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Apagando todos los actuadores");
    
    for (int i = 0; i < ACTUADOR_MAX_COUNT; i++) {
        vehiculo_actuador_desactivar((actuador_tipo_t)i);
    }
    
    ESP_LOGI(TAG, "Todos los actuadores apagados");
    return ESP_OK;
}

esp_err_t vehiculo_actuador_obtener_estado(actuador_tipo_t tipo, actuador_estado_t* estado)
{
    if (!g_actuadores || !g_actuadores->inicializado) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!validar_tipo_actuador(tipo) || !estado) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_actuadores->mutex_actuadores, pdMS_TO_TICKS(100)) == pdTRUE) {
        *estado = g_actuadores->runtime[tipo].estado;
        xSemaphoreGive(g_actuadores->mutex_actuadores);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

esp_err_t vehiculo_actuador_esta_activo(actuador_tipo_t tipo, bool* activo)
{
    if (!g_actuadores || !g_actuadores->inicializado) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!validar_tipo_actuador(tipo) || !activo) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_actuadores->mutex_actuadores, pdMS_TO_TICKS(100)) == pdTRUE) {
        *activo = g_actuadores->runtime[tipo].activo;
        xSemaphoreGive(g_actuadores->mutex_actuadores);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

esp_err_t vehiculo_activar_secuencia_alarma(uint32_t duracion_ms)
{
    ESP_LOGI(TAG, "Activando secuencia de alarma por %d ms", duracion_ms);
    
    esp_err_t ret1 = vehiculo_actuador_activar(ACTUADOR_SIRENA, duracion_ms);
    esp_err_t ret2 = vehiculo_actuador_activar(ACTUADOR_LUCES, duracion_ms);
    
    if (ret1 != ESP_OK || ret2 != ESP_OK) {
        ESP_LOGE(TAG, "Error activando secuencia de alarma: sirena=%s, luces=%s",
                 esp_err_to_name(ret1), esp_err_to_name(ret2));
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

esp_err_t vehiculo_activar_secuencia_panico(void)
{
    ESP_LOGW(TAG, "ACTIVANDO SECUENCIA DE PÁNICO");
    
    // Activar todos los actuadores de seguridad por tiempo máximo
    vehiculo_actuador_activar(ACTUADOR_SIRENA, 0);           // Sin límite (hasta timeout automático)
    vehiculo_actuador_activar(ACTUADOR_LUCES, 0);            // Sin límite
    vehiculo_actuador_activar(ACTUADOR_BLOQUEO_MOTOR, 0);    // Sin límite
    vehiculo_actuador_activar(ACTUADOR_TRACCION, 0);         // Sin límite
    vehiculo_actuador_activar(ACTUADOR_BUZZER, 30000);       // 30 segundos de buzzer
    
    return ESP_OK;
}

esp_err_t vehiculo_activar_secuencia_localizacion(uint32_t duracion_ms)
{
    ESP_LOGI(TAG, "Activando secuencia de localización por %d ms", duracion_ms);
    
    esp_err_t ret1 = vehiculo_actuador_activar(ACTUADOR_LUCES, duracion_ms);
    esp_err_t ret2 = vehiculo_actuador_activar(ACTUADOR_BUZZER, duracion_ms);
    
    if (ret1 != ESP_OK || ret2 != ESP_OK) {
        ESP_LOGE(TAG, "Error activando secuencia de localización");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

esp_err_t vehiculo_actuador_obtener_estadisticas(actuador_tipo_t tipo, 
                                                uint64_t* tiempo_total_activo,
                                                uint32_t* conteo_activaciones,
                                                uint32_t* conteo_errores)
{
    if (!g_actuadores || !g_actuadores->inicializado) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!validar_tipo_actuador(tipo)) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_actuadores->mutex_actuadores, pdMS_TO_TICKS(100)) == pdTRUE) {
        actuador_runtime_t* runtime = &g_actuadores->runtime[tipo];
        
        if (tiempo_total_activo) {
            *tiempo_total_activo = runtime->tiempo_total_activo;
            
            // Si está actualmente activo, sumar tiempo actual
            if (runtime->activo) {
                *tiempo_total_activo += (esp_timer_get_time() - runtime->tiempo_activacion_inicio);
            }
        }
        
        if (conteo_activaciones) {
            *conteo_activaciones = runtime->conteo_activaciones;
        }
        
        if (conteo_errores) {
            *conteo_errores = runtime->conteo_errores;
        }
        
        xSemaphoreGive(g_actuadores->mutex_actuadores);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

bool vehiculo_actuadores_verificar_integridad(char* reporte, size_t tamaño_buffer)
{
    if (!g_actuadores || !g_actuadores->inicializado) {
        if (reporte && tamaño_buffer > 0) {
            snprintf(reporte, tamaño_buffer, "Módulo no inicializado");
        }
        return false;
    }
    
    bool integridad_ok = true;
    size_t offset = 0;
    
    if (reporte && tamaño_buffer > 0) {
        offset += snprintf(reporte + offset, tamaño_buffer - offset, 
                          "=== REPORTE INTEGRIDAD ACTUADORES ===\n");
    }
    
    if (xSemaphoreTake(g_actuadores->mutex_actuadores, pdMS_TO_TICKS(500)) == pdTRUE) {
        
        for (int i = 0; i < ACTUADOR_MAX_COUNT; i++) {
            actuador_runtime_t* runtime = &g_actuadores->runtime[i];
            bool actuador_ok = true;
            
            // Verificar estado del actuador
            if (runtime->estado == ACTUADOR_ESTADO_ERROR || 
                runtime->estado == ACTUADOR_ESTADO_TIMEOUT) {
                actuador_ok = false;
                integridad_ok = false;
            }
            
            // Verificar número de errores
            if (runtime->conteo_errores > 5) {  // Más de 5 errores es sospechoso
                actuador_ok = false;
                integridad_ok = false;
            }
            
            if (reporte && tamaño_buffer > offset) {
                offset += snprintf(reporte + offset, tamaño_buffer - offset,
                                  "%s: %s (Activaciones: %" PRIu32 ", Errores: %" PRIu32 ")\n",
                                  g_actuadores->config[i].nombre,
                                  actuador_ok ? "OK" : "FALLA",
                                  runtime->conteo_activaciones,
                                  runtime->conteo_errores);
            }
        }
        
        xSemaphoreGive(g_actuadores->mutex_actuadores);
    } else {
        integridad_ok = false;
        if (reporte && tamaño_buffer > offset) {
            offset += snprintf(reporte + offset, tamaño_buffer - offset,
                              "ERROR: Timeout accediendo al mutex\n");
        }
    }
    
    if (reporte && tamaño_buffer > offset) {
        snprintf(reporte + offset, tamaño_buffer - offset,
                "INTEGRIDAD GENERAL: %s\n", integridad_ok ? "OK" : "FALLA");
    }
    
    return integridad_ok;
}

esp_err_t vehiculo_actuadores_autotest(uint32_t duracion_test_ms)
{
    if (!g_actuadores || !g_actuadores->inicializado) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Iniciando autotest de actuadores (%d ms por actuador)", duracion_test_ms);
    
    bool todos_ok = true;
    
    for (int i = 0; i < ACTUADOR_MAX_COUNT; i++) {
        ESP_LOGI(TAG, "Testing actuador %s...", g_actuadores->config[i].nombre);
        
        // Activar actuador por tiempo de test
        esp_err_t ret = vehiculo_actuador_activar((actuador_tipo_t)i, duracion_test_ms);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "FALLA activando %s: %s", 
                     g_actuadores->config[i].nombre, esp_err_to_name(ret));
            todos_ok = false;
            continue;
        }
        
        // Esperar a que se complete el test
        vTaskDelay(pdMS_TO_TICKS(duracion_test_ms + 100));
        
        // Verificar que se desactivó correctamente
        bool activo;
        ret = vehiculo_actuador_esta_activo((actuador_tipo_t)i, &activo);
        if (ret == ESP_OK && !activo) {
            ESP_LOGI(TAG, "✓ %s: PASS", g_actuadores->config[i].nombre);
        } else {
            ESP_LOGE(TAG, "✗ %s: FAIL (no se desactivó)", g_actuadores->config[i].nombre);
            todos_ok = false;
            
            // Forzar desactivación
            vehiculo_actuador_desactivar((actuador_tipo_t)i);
        }
    }
    
    ESP_LOGI(TAG, "Autotest completado: %s", todos_ok ? "TODOS OK" : "FALLOS DETECTADOS");
    return todos_ok ? ESP_OK : ESP_FAIL;
}

void vehiculo_actuadores_apagado_emergencia(void)
{
    ESP_LOGE(TAG, "APAGADO DE EMERGENCIA - Desactivando todos los actuadores");
    
    // Apagar directamente los GPIO sin usar mutex (emergencia)
    for (int i = 0; i < ACTUADOR_MAX_COUNT; i++) {
        if (g_actuadores && g_actuadores->config[i].pin != GPIO_NUM_NC) {
            gpio_set_level(g_actuadores->config[i].pin, 
                          !g_actuadores->config[i].activo_alto);
        }
    }
    
    ESP_LOGE(TAG, "Apagado de emergencia completado");
}

const vehiculo_actuadores_t* vehiculo_actuadores_obtener_instancia(void)
{
    return g_actuadores;
}

// ============================================================================
// FUNCIONES DE UTILIDAD
// ============================================================================

const char* vehiculo_actuador_tipo_a_string(actuador_tipo_t tipo)
{
    if (!validar_tipo_actuador(tipo) || !g_actuadores) {
        return "DESCONOCIDO";
    }
    
    return g_actuadores->config[tipo].nombre;
}

const char* vehiculo_actuador_estado_a_string(actuador_estado_t estado)
{
    switch (estado) {
        case ACTUADOR_ESTADO_INACTIVO: return "INACTIVO";
        case ACTUADOR_ESTADO_ACTIVO: return "ACTIVO";
        case ACTUADOR_ESTADO_ERROR: return "ERROR";
        case ACTUADOR_ESTADO_TIMEOUT: return "TIMEOUT";
        default: return "DESCONOCIDO";
    }
}
