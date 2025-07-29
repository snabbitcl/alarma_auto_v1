/**
 * @file vehiculo_sensores.c
 * @brief Implementación del sistema de sensores del vehículo
 * 
 * Sistema robusto de monitoreo de sensores con filtrado de ruido,
 * detección de patrones y manejo de eventos en tiempo real
 * 
 * @author Sistema Alarma Vehicular
 * @date 2024
 */

#include "vehiculo_sensores.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include <inttypes.h>
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include <string.h>
#include <math.h>

// ============================================================================
// VARIABLES GLOBALES Y CONFIGURACIÓN
// ============================================================================

static vehiculo_sensores_t* g_sensores = NULL;
static const char* TAG = SENSORES_TAG;

// Configuración por defecto de sensores digitales
static const sensor_digital_config_t SENSORES_DIGITALES_CONFIG_DEFAULT[SENSOR_DIGITAL_MAX_COUNT] = {
    [SENSOR_PUERTA_CONDUCTOR] = {
        .pin = GPIO_NUM_10,
        .activo_alto = false,           // Reed switch normalmente cerrado
        .pull_up = true,
        .debounce_ms = 100,
        .nombre = "Puerta Conductor",
        .critico = true
    },
    [SENSOR_PUERTA_PASAJERO] = {
        .pin = GPIO_NUM_11,
        .activo_alto = false,
        .pull_up = true,
        .debounce_ms = 100,
        .nombre = "Puerta Pasajero",
        .critico = true
    },
    [SENSOR_CAPO] = {
        .pin = GPIO_NUM_12,
        .activo_alto = false,
        .pull_up = true,
        .debounce_ms = 150,
        .nombre = "Capó",
        .critico = true
    },
    [SENSOR_BAUL] = {
        .pin = GPIO_NUM_13,
        .activo_alto = false,
        .pull_up = true,
        .debounce_ms = 150,
        .nombre = "Baúl",
        .critico = true
    },
    [SENSOR_MOVIMIENTO_PIR] = {
        .pin = GPIO_NUM_14,
        .activo_alto = true,            // PIR activo en alto
        .pull_up = false,
        .debounce_ms = 200,
        .nombre = "Movimiento PIR",
        .critico = true
    },
    [SENSOR_SHOCK] = {
        .pin = GPIO_NUM_15,
        .activo_alto = true,
        .pull_up = false,
        .debounce_ms = 50,              // Respuesta rápida para shock
        .nombre = "Sensor Shock",
        .critico = true
    },
    [SENSOR_TAMPER_CAJA] = {
        .pin = GPIO_NUM_17,
        .activo_alto = false,           // Tamper switch normalmente cerrado
        .pull_up = true,
        .debounce_ms = 50,              // Respuesta inmediata para tamper
        .nombre = "Tamper Caja",
        .critico = true
    }
};

// Configuración por defecto de sensores analógicos
static const sensor_analogico_config_t SENSORES_ANALOGICOS_CONFIG_DEFAULT[SENSOR_ANALOGICO_MAX_COUNT] = {
    [SENSOR_BATERIA_VOLTAJE] = {
        .canal_adc = ADC_CHANNEL_0,
        .factor_escala = 4,             // Divisor de tensión 1:4 (0-16.6V -> 0-3.3V)
        .offset_mv = 0,
        .umbral_bajo_mv = SENSORES_UMBRAL_BATERIA_BAJA_MV,
        .umbral_alto_mv = 14400,        // 14.4V máximo carga
        .nombre = "Batería Principal",
        .unidad = "V"
    },
    [SENSOR_CORRIENTE_SISTEMA] = {
        .canal_adc = ADC_CHANNEL_1,
        .factor_escala = 100,           // 100mV/A (sensor de corriente)
        .offset_mv = 2500,              // Offset de 2.5V (corriente cero)
        .umbral_bajo_mv = 2000,         // -500mA
        .umbral_alto_mv = 3000,         // +500mA
        .nombre = "Corriente Sistema",
        .unidad = "mA"
    },
    [SENSOR_TEMPERATURA_INTERNA] = {
        .canal_adc = ADC_CHANNEL_2,
        .factor_escala = 10,            // 10mV/°C (sensor LM35)
        .offset_mv = 0,
        .umbral_bajo_mv = -10000,       // -10°C (no crítico)
        .umbral_alto_mv = 70000,        // 70°C crítico
        .nombre = "Temperatura Interna",
        .unidad = "°C"
    }
};

// Handles para la nueva API de ADC
static adc_oneshot_unit_handle_t g_adc1_handle = NULL;
static adc_cali_handle_t g_adc_cali_handle = NULL;

// ============================================================================
// FUNCIONES PRIVADAS
// ============================================================================

/**
 * @brief Inicializa GPIO para sensores digitales
 */
static esp_err_t init_gpio_sensores_digitales(void)
{
    ESP_LOGI(TAG, "Inicializando GPIO para sensores digitales");
    
    for (int i = 0; i < SENSOR_DIGITAL_MAX_COUNT; i++) {
        gpio_config_t io_conf = {
            .intr_type = GPIO_INTR_ANYEDGE,  // Interrupciones en ambos flancos
            .mode = GPIO_MODE_INPUT,
            .pin_bit_mask = (1ULL << g_sensores->config_digital[i].pin),
            .pull_down_en = !g_sensores->config_digital[i].pull_up,
            .pull_up_en = g_sensores->config_digital[i].pull_up
        };
        
        esp_err_t ret = gpio_config(&io_conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Error configurando GPIO %d para %s: %s",
                     g_sensores->config_digital[i].pin,
                     g_sensores->config_digital[i].nombre,
                     esp_err_to_name(ret));
            return ret;
        }
        
        ESP_LOGI(TAG, "Sensor digital %s configurado en GPIO %d",
                 g_sensores->config_digital[i].nombre,
                 g_sensores->config_digital[i].pin);
    }
    
    return ESP_OK;
}

/**
 * @brief Inicializa ADC para sensores analógicos
 */
static esp_err_t init_adc_sensores_analogicos(void)
{
    ESP_LOGI(TAG, "Inicializando ADC para sensores analógicos");
    
    // Configurar ADC1 con nueva API
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t ret = adc_oneshot_new_unit(&init_config1, &g_adc1_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error inicializando ADC1: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Configurar canales ADC
    for (int i = 0; i < SENSOR_ANALOGICO_MAX_COUNT; i++) {
        adc_oneshot_chan_cfg_t config = {
            .bitwidth = SENSORES_ADC_BITWIDTH,
            .atten = SENSORES_ADC_ATTEN,
        };
        
        ret = adc_oneshot_config_channel(g_adc1_handle, g_sensores->config_analogico[i].canal_adc, &config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Error configurando canal ADC %d: %s", 
                     g_sensores->config_analogico[i].canal_adc, esp_err_to_name(ret));
            return ret;
        }
        
        ESP_LOGI(TAG, "Sensor analógico %s configurado en canal ADC %d",
                 g_sensores->config_analogico[i].nombre,
                 g_sensores->config_analogico[i].canal_adc);
    }
    
    // Inicializar calibración ADC con nueva API
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = SENSORES_ADC_ATTEN,
        .bitwidth = SENSORES_ADC_BITWIDTH,
    };
    
    ret = adc_cali_create_scheme_curve_fitting(&cali_config, &g_adc_cali_handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibración ADC inicializada con curve fitting");
    } else {
        ESP_LOGW(TAG, "Calibración ADC no disponible, usando valores sin calibrar");
        g_adc_cali_handle = NULL;
    }
    
    return ESP_OK;
}

/**
 * @brief Lee un sensor digital con debounce
 */
static bool leer_sensor_digital_con_debounce(sensor_digital_tipo_t tipo)
{
    sensor_digital_runtime_t* runtime = &g_sensores->runtime_digital[tipo];
    const sensor_digital_config_t* config = &g_sensores->config_digital[tipo];
    
    bool valor_raw = gpio_get_level(config->pin);
    bool valor_logico = config->activo_alto ? valor_raw : !valor_raw;
    
    uint64_t tiempo_actual = esp_timer_get_time();
    
    // Verificar si estamos en período de debounce
    if (runtime->debounce_activo) {
        if ((tiempo_actual - runtime->timestamp_debounce) < (config->debounce_ms * 1000)) {
            return runtime->valor_actual;  // Mantener valor anterior durante debounce
        }
        runtime->debounce_activo = false;
    }
    
    // Si el valor cambió, iniciar debounce
    if (valor_logico != runtime->valor_actual) {
        runtime->debounce_activo = true;
        runtime->timestamp_debounce = tiempo_actual;
        return runtime->valor_actual;  // Mantener valor anterior
    }
    
    return valor_logico;
}

/**
 * @brief Lee un sensor analógico con filtrado
 */
static uint32_t leer_sensor_analogico_con_filtro(sensor_analogico_tipo_t tipo)
{
    sensor_analogico_runtime_t* runtime = &g_sensores->runtime_analogico[tipo];
    const sensor_analogico_config_t* config = &g_sensores->config_analogico[tipo];
    
    // Tomar múltiples muestras para promedio
    uint32_t suma_muestras = 0;
    for (int i = 0; i < SENSORES_ADC_SAMPLES_AVG; i++) {
        int adc_reading = 0;
        esp_err_t ret = adc_oneshot_read(g_adc1_handle, config->canal_adc, &adc_reading);
        if (ret != ESP_OK) {
            runtime->conteo_errores++;
            return runtime->valor_filtrado_mv;  // Retornar último valor válido
        }
        suma_muestras += adc_reading;
    }
    
    // Convertir a mV usando calibración
    uint32_t adc_promedio = suma_muestras / SENSORES_ADC_SAMPLES_AVG;
    uint32_t voltaje_mv = 0;
    
    if (g_adc_cali_handle != NULL) {
        esp_err_t ret = adc_cali_raw_to_voltage(g_adc_cali_handle, adc_promedio, (int*)&voltaje_mv);
        if (ret != ESP_OK) {
            // Si falla la calibración, usar conversión aproximada
            voltaje_mv = (adc_promedio * 3300) / 4095;  // Para 12 bits y 3.3V
        }
    } else {
        // Sin calibración, conversión aproximada
        voltaje_mv = (adc_promedio * 3300) / 4095;  // Para 12 bits y 3.3V
    }
    
    // Aplicar factor de escala y offset
    voltaje_mv = (voltaje_mv * config->factor_escala) + config->offset_mv;
    
    // Actualizar buffer circular para filtro de media móvil
    runtime->muestras_buffer[runtime->indice_buffer] = voltaje_mv;
    runtime->indice_buffer = (runtime->indice_buffer + 1) % SENSORES_FILTRO_VENTANA_MUESTRAS;
    
    // Calcular media móvil
    uint64_t suma_filtro = 0;
    for (int i = 0; i < SENSORES_FILTRO_VENTANA_MUESTRAS; i++) {
        suma_filtro += runtime->muestras_buffer[i];
    }
    
    uint32_t valor_filtrado = suma_filtro / SENSORES_FILTRO_VENTANA_MUESTRAS;
    
    return valor_filtrado;
}

/**
 * @brief Procesa eventos de sensores digitales
 */
static void procesar_eventos_sensores_digitales(void)
{
    for (int i = 0; i < SENSOR_DIGITAL_MAX_COUNT; i++) {
        sensor_digital_runtime_t* runtime = &g_sensores->runtime_digital[i];
        bool nuevo_valor = leer_sensor_digital_con_debounce((sensor_digital_tipo_t)i);
        
        // Detectar cambio de estado
        if (nuevo_valor != runtime->valor_actual) {
            uint64_t tiempo_actual = esp_timer_get_time();
            
            // Actualizar estadísticas si se activó
            if (nuevo_valor) {
                runtime->conteo_activaciones++;
                runtime->timestamp_ultimo_cambio = tiempo_actual;
            } else if (runtime->valor_actual) {
                // Se desactivó - acumular tiempo activo
                runtime->tiempo_activacion_total += 
                    (tiempo_actual - runtime->timestamp_ultimo_cambio);
            }
            
            runtime->valor_anterior = runtime->valor_actual;
            runtime->valor_actual = nuevo_valor;
            runtime->estado = nuevo_valor ? SENSOR_ESTADO_ACTIVADO : SENSOR_ESTADO_NORMAL;
            
            // Generar evento correspondiente
            sensor_evento_t evento = {
                .timestamp = tiempo_actual,
                .datos.digital.tipo_sensor = (sensor_digital_tipo_t)i,
                .datos.digital.valor = nuevo_valor
            };
            
            // Determinar tipo de evento específico
            switch (i) {
                case SENSOR_PUERTA_CONDUCTOR:
                case SENSOR_PUERTA_PASAJERO:
                    evento.tipo_evento = SENSOR_EVENTO_PUERTA_ABIERTA;
                    break;
                case SENSOR_CAPO:
                    evento.tipo_evento = SENSOR_EVENTO_CAPO_ABIERTO;
                    break;
                case SENSOR_BAUL:
                    evento.tipo_evento = SENSOR_EVENTO_BAUL_ABIERTO;
                    break;
                case SENSOR_MOVIMIENTO_PIR:
                    evento.tipo_evento = SENSOR_EVENTO_MOVIMIENTO_DETECTADO;
                    break;
                case SENSOR_SHOCK:
                    evento.tipo_evento = SENSOR_EVENTO_SHOCK_DETECTADO;
                    break;
                case SENSOR_TAMPER_CAJA:
                    evento.tipo_evento = SENSOR_EVENTO_TAMPER_DETECTADO;
                    break;
                default:
                    evento.tipo_evento = SENSOR_EVENTO_ERROR_SENSOR;
                    break;
            }
            
            // Enviar evento a la cola solo si se activó (para evitar spam)
            if (nuevo_valor) {
                if (xQueueSend(g_sensores->cola_eventos, &evento, 0) != pdTRUE) {
                    ESP_LOGW(TAG, "Cola de eventos llena - evento perdido");
                }
                g_sensores->conteo_eventos_totales++;
            }
            
            ESP_LOGI(TAG, "Sensor %s: %s",
                     g_sensores->config_digital[i].nombre,
                     nuevo_valor ? "ACTIVADO" : "DESACTIVADO");
        }
    }
}

/**
 * @brief Procesa eventos de sensores analógicos
 */
static void procesar_eventos_sensores_analogicos(void)
{
    for (int i = 0; i < SENSOR_ANALOGICO_MAX_COUNT; i++) {
        sensor_analogico_runtime_t* runtime = &g_sensores->runtime_analogico[i];
        const sensor_analogico_config_t* config = &g_sensores->config_analogico[i];
        
        uint32_t nuevo_valor = leer_sensor_analogico_con_filtro((sensor_analogico_tipo_t)i);
        
        // Actualizar estadísticas
        runtime->valor_actual_mv = nuevo_valor;
        runtime->valor_filtrado_mv = nuevo_valor;
        runtime->conteo_lecturas++;
        runtime->timestamp_ultima_lectura = esp_timer_get_time();
        
        // Actualizar mínimos y máximos
        if (nuevo_valor < runtime->valor_minimo_mv || runtime->valor_minimo_mv == 0) {
            runtime->valor_minimo_mv = nuevo_valor;
        }
        if (nuevo_valor > runtime->valor_maximo_mv) {
            runtime->valor_maximo_mv = nuevo_valor;
        }
        
        // Verificar umbrales
        bool evento_generado = false;
        sensor_evento_t evento = {
            .timestamp = runtime->timestamp_ultima_lectura,
            .datos.analogico.tipo_sensor = (sensor_analogico_tipo_t)i,
            .datos.analogico.valor_mv = nuevo_valor
        };
        
        if (i == SENSOR_BATERIA_VOLTAJE) {
            if (nuevo_valor < SENSORES_UMBRAL_BATERIA_CRITICA_MV) {
                evento.tipo_evento = SENSOR_EVENTO_BATERIA_CRITICA;
                runtime->estado = SENSOR_ESTADO_ERROR;
                evento_generado = true;
            } else if (nuevo_valor < config->umbral_bajo_mv) {
                evento.tipo_evento = SENSOR_EVENTO_BATERIA_BAJA;
                runtime->estado = SENSOR_ESTADO_ACTIVADO;
                evento_generado = true;
            } else {
                runtime->estado = SENSOR_ESTADO_NORMAL;
            }
        } else if (i == SENSOR_TEMPERATURA_INTERNA) {
            if (nuevo_valor > config->umbral_alto_mv) {
                evento.tipo_evento = SENSOR_EVENTO_TEMPERATURA_ALTA;
                runtime->estado = SENSOR_ESTADO_ERROR;
                evento_generado = true;
            } else {
                runtime->estado = SENSOR_ESTADO_NORMAL;
            }
        }
        
        // Enviar evento si se generó
        if (evento_generado) {
            if (xQueueSend(g_sensores->cola_eventos, &evento, 0) != pdTRUE) {
                ESP_LOGW(TAG, "Cola de eventos llena - evento analógico perdido");
            }
            g_sensores->conteo_eventos_totales++;
            
            ESP_LOGW(TAG, "Sensor %s: Umbral excedido (%d mV)",
                     config->nombre, nuevo_valor);
        }
    }
}

/**
 * @brief Tarea principal de monitoreo de sensores
 */
static void task_monitoreo_sensores(void* pvParameters)
{
    ESP_LOGI(TAG, "Iniciando tarea de monitoreo de sensores");
    
    TickType_t xLastWakeTime = xTaskGetTickCount();
    
    while (g_sensores && g_sensores->monitoreo_activo) {
        // Tomar mutex para acceso thread-safe
        if (xSemaphoreTake(g_sensores->mutex_sensores, pdMS_TO_TICKS(100)) == pdTRUE) {
            
            // Procesar sensores digitales
            procesar_eventos_sensores_digitales();
            
            // Procesar sensores analógicos  
            procesar_eventos_sensores_analogicos();
            
            // Actualizar timestamp global
            g_sensores->timestamp_ultima_lectura_global = esp_timer_get_time();
            g_sensores->conteo_lecturas_totales++;
            
            xSemaphoreGive(g_sensores->mutex_sensores);
        } else {
            ESP_LOGW(TAG, "Timeout accediendo mutex en tarea de sensores");
            g_sensores->conteo_errores_totales++;
        }
        
        // Esperar hasta el próximo ciclo
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(g_sensores->intervalo_lectura_ms));
    }
    
    ESP_LOGI(TAG, "Tarea de monitoreo de sensores terminada");
    vTaskDelete(NULL);
}

// ============================================================================
// FUNCIONES PÚBLICAS
// ============================================================================

esp_err_t vehiculo_sensores_init(void)
{
    if (g_sensores != NULL) {
        ESP_LOGW(TAG, "Módulo de sensores ya inicializado");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Inicializando módulo de sensores del vehículo");
    
    // Alocar estructura principal
    g_sensores = calloc(1, sizeof(vehiculo_sensores_t));
    if (!g_sensores) {
        ESP_LOGE(TAG, "Error asignando memoria para sensores");
        return ESP_ERR_NO_MEM;
    }
    
    // Copiar configuración por defecto
    memcpy(g_sensores->config_digital, SENSORES_DIGITALES_CONFIG_DEFAULT,
           sizeof(SENSORES_DIGITALES_CONFIG_DEFAULT));
    memcpy(g_sensores->config_analogico, SENSORES_ANALOGICOS_CONFIG_DEFAULT,
           sizeof(SENSORES_ANALOGICOS_CONFIG_DEFAULT));
    
    // Configuración inicial
    g_sensores->intervalo_lectura_ms = SENSORES_INTERVALO_LECTURA_MS;
    g_sensores->filtro_ruido_habilitado = true;
    g_sensores->umbral_ruido = 50;  // 50mV de umbral de ruido por defecto
    
    // Crear objetos de sincronización
    g_sensores->mutex_sensores = xSemaphoreCreateMutex();
    if (!g_sensores->mutex_sensores) {
        ESP_LOGE(TAG, "Error creando mutex de sensores");
        free(g_sensores);
        g_sensores = NULL;
        return ESP_ERR_NO_MEM;
    }
    
    g_sensores->cola_eventos = xQueueCreate(20, sizeof(sensor_evento_t));
    if (!g_sensores->cola_eventos) {
        ESP_LOGE(TAG, "Error creando cola de eventos");
        vSemaphoreDelete(g_sensores->mutex_sensores);
        free(g_sensores);
        g_sensores = NULL;
        return ESP_ERR_NO_MEM;
    }
    
    // Inicializar estado runtime
    for (int i = 0; i < SENSOR_DIGITAL_MAX_COUNT; i++) {
        g_sensores->runtime_digital[i].estado = SENSOR_ESTADO_NORMAL;
        g_sensores->runtime_digital[i].valor_actual = false;
        g_sensores->runtime_digital[i].valor_anterior = false;
    }
    
    for (int i = 0; i < SENSOR_ANALOGICO_MAX_COUNT; i++) {
        g_sensores->runtime_analogico[i].estado = SENSOR_ESTADO_NORMAL;
        g_sensores->runtime_analogico[i].valor_minimo_mv = UINT32_MAX;
        g_sensores->runtime_analogico[i].valor_maximo_mv = 0;
        // Inicializar buffer circular con valores por defecto
        for (int j = 0; j < SENSORES_FILTRO_VENTANA_MUESTRAS; j++) {
            g_sensores->runtime_analogico[i].muestras_buffer[j] = 0;
        }
    }
    
    // Inicializar hardware
    esp_err_t ret = init_gpio_sensores_digitales();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error inicializando GPIO de sensores digitales");
        vehiculo_sensores_deinit();
        return ret;
    }
    
    ret = init_adc_sensores_analogicos();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error inicializando ADC de sensores analógicos");
        vehiculo_sensores_deinit();
        return ret;
    }
    
    g_sensores->inicializado = true;
    g_sensores->timestamp_ultima_lectura_global = esp_timer_get_time();
    
    ESP_LOGI(TAG, "Módulo de sensores inicializado correctamente");
    return ESP_OK;
}

esp_err_t vehiculo_sensores_deinit(void)
{
    if (!g_sensores) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Desinicializando módulo de sensores");
    
    // Detener monitoreo
    vehiculo_sensores_detener_monitoreo();
    
    // Liberar recursos
    if (g_sensores->mutex_sensores) {
        vSemaphoreDelete(g_sensores->mutex_sensores);
    }
    
    if (g_sensores->cola_eventos) {
        vQueueDelete(g_sensores->cola_eventos);
    }
    
    // Liberar recursos ADC con nueva API
    if (g_adc_cali_handle) {
        adc_cali_delete_scheme_curve_fitting(g_adc_cali_handle);
        g_adc_cali_handle = NULL;
    }
    
    if (g_adc1_handle) {
        adc_oneshot_del_unit(g_adc1_handle);
        g_adc1_handle = NULL;
    }
    
    free(g_sensores);
    g_sensores = NULL;
    
    ESP_LOGI(TAG, "Módulo de sensores desinicializado");
    return ESP_OK;
}

esp_err_t vehiculo_sensores_iniciar_monitoreo(void)
{
    if (!g_sensores || !g_sensores->inicializado) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (g_sensores->monitoreo_activo) {
        ESP_LOGW(TAG, "Monitoreo ya está activo");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Iniciando monitoreo activo de sensores");
    
    g_sensores->monitoreo_activo = true;
    
    // Crear tarea de monitoreo
    BaseType_t ret = xTaskCreate(task_monitoreo_sensores,
                                "sensores_monitor",
                                4096,
                                NULL,
                                tskIDLE_PRIORITY + 3,  // Prioridad media
                                &g_sensores->task_handle);
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Error creando tarea de monitoreo de sensores");
        g_sensores->monitoreo_activo = false;
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Monitoreo de sensores iniciado");
    return ESP_OK;
}

esp_err_t vehiculo_sensores_detener_monitoreo(void)
{
    if (!g_sensores) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!g_sensores->monitoreo_activo) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Deteniendo monitoreo de sensores");
    
    g_sensores->monitoreo_activo = false;
    
    // Esperar a que termine la tarea (timeout de 5 segundos)
    if (g_sensores->task_handle) {
        for (int i = 0; i < 50; i++) {  // 50 * 100ms = 5 segundos
            if (eTaskGetState(g_sensores->task_handle) == eDeleted) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        
        // Si no terminó, forzar eliminación
        if (eTaskGetState(g_sensores->task_handle) != eDeleted) {
            ESP_LOGW(TAG, "Forzando eliminación de tarea de sensores");
            vTaskDelete(g_sensores->task_handle);
        }
        
        g_sensores->task_handle = NULL;
    }
    
    ESP_LOGI(TAG, "Monitoreo de sensores detenido");
    return ESP_OK;
}

esp_err_t vehiculo_sensor_digital_leer(sensor_digital_tipo_t tipo, bool* estado)
{
    if (!g_sensores || !g_sensores->inicializado) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (tipo >= SENSOR_DIGITAL_MAX_COUNT || !estado) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_sensores->mutex_sensores, pdMS_TO_TICKS(100)) == pdTRUE) {
        *estado = g_sensores->runtime_digital[tipo].valor_actual;
        xSemaphoreGive(g_sensores->mutex_sensores);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

esp_err_t vehiculo_sensor_analogico_leer(sensor_analogico_tipo_t tipo, uint32_t* valor_mv)
{
    if (!g_sensores || !g_sensores->inicializado) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (tipo >= SENSOR_ANALOGICO_MAX_COUNT || !valor_mv) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_sensores->mutex_sensores, pdMS_TO_TICKS(100)) == pdTRUE) {
        *valor_mv = g_sensores->runtime_analogico[tipo].valor_filtrado_mv;
        xSemaphoreGive(g_sensores->mutex_sensores);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

esp_err_t vehiculo_sensores_obtener_evento(sensor_evento_t* evento, uint32_t timeout_ms)
{
    if (!g_sensores || !g_sensores->inicializado || !evento) {
        return ESP_ERR_INVALID_ARG;
    }
    
    TickType_t timeout_ticks = (timeout_ms == 0) ? 0 : 
                              (timeout_ms == UINT32_MAX) ? portMAX_DELAY :
                              pdMS_TO_TICKS(timeout_ms);
    
    if (xQueueReceive(g_sensores->cola_eventos, evento, timeout_ticks) == pdTRUE) {
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

bool vehiculo_sensores_verificar_integridad(char* reporte, size_t tamaño_buffer)
{
    if (!g_sensores || !g_sensores->inicializado) {
        if (reporte && tamaño_buffer > 0) {
            snprintf(reporte, tamaño_buffer, "Módulo no inicializado");
        }
        return false;
    }
    
    bool integridad_ok = true;
    size_t offset = 0;
    
    if (reporte && tamaño_buffer > 0) {
        offset += snprintf(reporte + offset, tamaño_buffer - offset,
                          "=== REPORTE INTEGRIDAD SENSORES ===\n");
    }
    
    if (xSemaphoreTake(g_sensores->mutex_sensores, pdMS_TO_TICKS(500)) == pdTRUE) {
        
        // Verificar sensores digitales
        for (int i = 0; i < SENSOR_DIGITAL_MAX_COUNT; i++) {
            sensor_digital_runtime_t* runtime = &g_sensores->runtime_digital[i];
            bool sensor_ok = (runtime->estado != SENSOR_ESTADO_ERROR &&
                             runtime->estado != SENSOR_ESTADO_DESCONECTADO);
            
            if (!sensor_ok) {
                integridad_ok = false;
            }
            
            if (reporte && tamaño_buffer > offset) {
                offset += snprintf(reporte + offset, tamaño_buffer - offset,
                                  "%s: %s (Activaciones: %" PRIu32 ")\n",
                                  g_sensores->config_digital[i].nombre,
                                  sensor_ok ? "OK" : "FALLA",
                                  runtime->conteo_activaciones);
            }
        }
        
        // Verificar sensores analógicos
        for (int i = 0; i < SENSOR_ANALOGICO_MAX_COUNT; i++) {
            sensor_analogico_runtime_t* runtime = &g_sensores->runtime_analogico[i];
            bool sensor_ok = (runtime->estado != SENSOR_ESTADO_ERROR &&
                             runtime->conteo_errores < 10);
            
            if (!sensor_ok) {
                integridad_ok = false;
            }
            
            if (reporte && tamaño_buffer > offset) {
                offset += snprintf(reporte + offset, tamaño_buffer - offset,
                                  "%s: %s (%" PRIu32 " %s)\n",
                                  g_sensores->config_analogico[i].nombre,
                                  sensor_ok ? "OK" : "FALLA",
                                  runtime->valor_filtrado_mv,
                                  g_sensores->config_analogico[i].unidad);
            }
        }
        
        xSemaphoreGive(g_sensores->mutex_sensores);
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

// ============================================================================
// FUNCIONES DE UTILIDAD
// ============================================================================

const char* vehiculo_sensor_digital_tipo_a_string(sensor_digital_tipo_t tipo)
{
    if (tipo >= SENSOR_DIGITAL_MAX_COUNT || !g_sensores) {
        return "DESCONOCIDO";
    }
    
    return g_sensores->config_digital[tipo].nombre;
}

const char* vehiculo_sensor_analogico_tipo_a_string(sensor_analogico_tipo_t tipo)
{
    if (tipo >= SENSOR_ANALOGICO_MAX_COUNT || !g_sensores) {
        return "DESCONOCIDO";
    }
    
    return g_sensores->config_analogico[tipo].nombre;
}

const char* vehiculo_sensor_estado_a_string(sensor_estado_t estado)
{
    switch (estado) {
        case SENSOR_ESTADO_NORMAL: return "NORMAL";
        case SENSOR_ESTADO_ACTIVADO: return "ACTIVADO";
        case SENSOR_ESTADO_ERROR: return "ERROR";
        case SENSOR_ESTADO_DESCONECTADO: return "DESCONECTADO";
        default: return "DESCONOCIDO";
    }
}

const char* vehiculo_sensor_evento_a_string(sensor_eventos_t evento)
{
    switch (evento) {
        case SENSOR_EVENTO_PUERTA_ABIERTA: return "PUERTA_ABIERTA";
        case SENSOR_EVENTO_CAPO_ABIERTO: return "CAPO_ABIERTO";
        case SENSOR_EVENTO_BAUL_ABIERTO: return "BAUL_ABIERTO";
        case SENSOR_EVENTO_MOVIMIENTO_DETECTADO: return "MOVIMIENTO_DETECTADO";
        case SENSOR_EVENTO_SHOCK_DETECTADO: return "SHOCK_DETECTADO";
        case SENSOR_EVENTO_TAMPER_DETECTADO: return "TAMPER_DETECTADO";
        case SENSOR_EVENTO_BATERIA_BAJA: return "BATERIA_BAJA";
        case SENSOR_EVENTO_BATERIA_CRITICA: return "BATERIA_CRITICA";
        case SENSOR_EVENTO_TEMPERATURA_ALTA: return "TEMPERATURA_ALTA";
        case SENSOR_EVENTO_ERROR_SENSOR: return "ERROR_SENSOR";
        default: return "DESCONOCIDO";
    }
}
