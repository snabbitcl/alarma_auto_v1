/**
 * @file app_vehiculo.c
 * @brief Implementación principal del módulo vehicular
 * 
 * Sistema de alarma vehicular ESP32-S3 con comunicación LoRa AU915
 * Arquitectura multi-tarea con seguridad crítica
 * 
 * @author Sistema Alarma Vehicular
 * @date 2024
 */

#include "app_vehiculo.h"
#include <inttypes.h>
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_ota_ops.h"
#include "esp_task_wdt.h"
#include "esp_efuse.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "nvs.h"
#include "nvs_flash.h"

// Componentes específicos del vehículo
#include "vehiculo_actuadores.h"
#include "vehiculo_sensores.h"
#include "vehiculo_seguridad.h"

// ============================================================================
// VARIABLES GLOBALES
// ============================================================================

static vehiculo_app_t* g_vehiculo_app = NULL;
static const char* TAG = VEHICULO_TAG;

// Handles para ADC con nueva API
static adc_oneshot_unit_handle_t g_adc1_handle = NULL;

// Configuración por defecto usando la estructura del componente actuadores (uso interno)
static const struct vehiculo_gpio_interno {
    gpio_num_t pin_rele_sirena;
    gpio_num_t pin_rele_luces; 
    gpio_num_t pin_rele_bloqueo;
    gpio_num_t pin_rele_gps;
    gpio_num_t pin_buzzer;
    gpio_num_t pin_led_estado;
    uint32_t tiempo_activacion_max_ms;
} __attribute__((unused)) g_vehiculo_gpio_interno = {
    .pin_rele_sirena = CONFIG_GPIO_RELE_SIRENA,
    .pin_rele_luces = CONFIG_GPIO_RELE_LUCES,
    .pin_rele_bloqueo = CONFIG_GPIO_RELE_BLOQUEO,
    .pin_rele_gps = CONFIG_GPIO_RELE_GPS,
    .pin_buzzer = CONFIG_GPIO_BUZZER,
    .pin_led_estado = CONFIG_GPIO_LED_ESTADO,
    .tiempo_activacion_max_ms = 300000  // 5 minutos máximo
};

static const sensor_config_t __attribute__((unused)) SENSOR_CONFIG_DEFAULT = {
    .pin_puerta_conductor = CONFIG_GPIO_PUERTA_CONDUCTOR,
    .pin_puerta_pasajero = CONFIG_GPIO_PUERTA_PASAJERO,
    .pin_capo = CONFIG_GPIO_CAPO,
    .pin_baul = CONFIG_GPIO_BAUL,
    .pin_movimiento_pir = CONFIG_GPIO_PIR,
    .pin_shock_sensor = CONFIG_GPIO_SHOCK,
    .pin_bateria_monitor = CONFIG_GPIO_ADC_BATERIA,
    .adc_bateria = CONFIG_ADC_CANAL_BATERIA,
    .pin_tamper_caja = CONFIG_GPIO_TAMPER,
    .umbral_bateria_baja_mv = 11000,    // 11V
    .sensibilidad_shock = 50            // Umbral de vibración
};

// ============================================================================
// FUNCIONES PRIVADAS
// ============================================================================

/**
 * @brief Inicializa la configuración NVS
 */
static esp_err_t vehiculo_init_nvs(void)
{
    esp_err_t ret = nvs_flash_secure_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_secure_init();
    }
    if (ret == ESP_ERR_NOT_SUPPORTED) {
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "NVS seguro inicializado correctamente");
    return ESP_OK;
}

/**
 * @brief Inicializa GPIO para actuadores
 */
static esp_err_t vehiculo_init_gpio_actuadores(void)
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 0,
        .pull_down_en = 0,
        .pull_up_en = 0
    };
    
    // Configurar pines de relés
    io_conf.pin_bit_mask = (1ULL << g_vehiculo_app->config_actuadores.pin_rele1) |
                          (1ULL << g_vehiculo_app->config_actuadores.pin_rele2) |
                          (1ULL << g_vehiculo_app->config_actuadores.pin_rele3) |
                          (1ULL << g_vehiculo_app->config_actuadores.pin_rele4) |
                          (1ULL << g_vehiculo_app->config_actuadores.pin_buzzer) |
                          (1ULL << g_vehiculo_app->config_actuadores.pin_led_estado);
    
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    
    // Inicializar todos los relés en estado OFF (seguridad)
    gpio_set_level(g_vehiculo_app->config_actuadores.pin_rele1, 0);
    gpio_set_level(g_vehiculo_app->config_actuadores.pin_rele2, 0);
    gpio_set_level(g_vehiculo_app->config_actuadores.pin_rele3, 0);
    gpio_set_level(g_vehiculo_app->config_actuadores.pin_rele4, 0);
    gpio_set_level(g_vehiculo_app->config_actuadores.pin_buzzer, 0);
    gpio_set_level(g_vehiculo_app->config_actuadores.pin_led_estado, 0);
    
    ESP_LOGI(TAG, "GPIO actuadores inicializados");
    return ESP_OK;
}

/**
 * @brief Inicializa GPIO para sensores
 */
static esp_err_t vehiculo_init_gpio_sensores(void)
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_ANYEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = 0,
        .pull_down_en = 0,
        .pull_up_en = 1
    };
    
    // Configurar pines de sensores digitales
    io_conf.pin_bit_mask = (1ULL << g_vehiculo_app->config_sensores.pin_puerta_conductor) |
                          (1ULL << g_vehiculo_app->config_sensores.pin_puerta_pasajero) |
                          (1ULL << g_vehiculo_app->config_sensores.pin_capo) |
                          (1ULL << g_vehiculo_app->config_sensores.pin_baul) |
                          (1ULL << g_vehiculo_app->config_sensores.pin_movimiento_pir) |
                          (1ULL << g_vehiculo_app->config_sensores.pin_shock_sensor) |
                          (1ULL << g_vehiculo_app->config_sensores.pin_tamper_caja);
    
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    
    // Configurar ADC para monitoreo de batería - Nueva API
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t ret = adc_oneshot_new_unit(&init_config1, &g_adc1_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error inicializando ADC1: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Configurar canal de batería
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,
    };
    ret = adc_oneshot_config_channel(g_adc1_handle, g_vehiculo_app->config_sensores.adc_bateria, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error configurando canal ADC: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "GPIO sensores inicializados");
    return ESP_OK;
}

/**
 * @brief Callback para heartbeat timer
 */
static void vehiculo_heartbeat_callback(void* arg)
{
    if (g_vehiculo_app && g_vehiculo_app->eventos_sistema) {
        xEventGroupSetBits(g_vehiculo_app->eventos_sistema, EVT_HEARTBEAT);
    }
}

/**
 * @brief Callback para timeout de autenticación
 */
static void vehiculo_timeout_autenticacion_callback(void* arg)
{
    ESP_LOGW(TAG, "Timeout de autenticación - activando medidas de seguridad");
    if (g_vehiculo_app && g_vehiculo_app->eventos_sistema) {
        xEventGroupSetBits(g_vehiculo_app->eventos_sistema, EVT_COMUNICACION_PERDIDA);
    }
}

/**
 * @brief Cargar configuración desde NVS
 */
static esp_err_t vehiculo_cargar_configuracion(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;
    
    err = nvs_open("vehiculo_cfg", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Usando configuración por defecto");
        return ESP_OK;
    }
    
    size_t required_size;
    
    // Cargar clave maestra si existe
    required_size = sizeof(g_vehiculo_app->clave_maestra);
    err = nvs_get_blob(nvs_handle, "clave_maestra", 
                       g_vehiculo_app->clave_maestra, &required_size);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Clave maestra no encontrada en NVS");
    }
    
    // Cargar ID de vehículo
    required_size = sizeof(g_vehiculo_app->id_vehiculo);
    err = nvs_get_blob(nvs_handle, "id_vehiculo", 
                       g_vehiculo_app->id_vehiculo, &required_size);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ID vehículo no encontrado en NVS");
    }
    
    nvs_close(nvs_handle);
    return ESP_OK;
}

// ============================================================================
// IMPLEMENTACIÓN DE TAREAS FREERTOS
// ============================================================================

/**
 * @brief Tarea de seguridad crítica (CPU1)
 * Maneja autenticación, cifrado y protocolos de seguridad
 */
void vehiculo_task_seguridad(void* pvParameters)
{
    ESP_LOGI(TAG, "Iniciando tarea de seguridad (CPU1)");
    
    esp_task_wdt_add(NULL);
    
    // Iniciar monitoreo de seguridad
    vehiculo_seguridad_iniciar_monitoreo();
    
    vehiculo_comando_t comando;
    evento_seguridad_t evento_seguridad;
    EventBits_t eventos;
    
    while (1) {
        esp_task_wdt_reset();
        
        // Procesar eventos de seguridad
        if (vehiculo_seguridad_obtener_evento(&evento_seguridad, 500) == ESP_OK) {
            ESP_LOGW(TAG, "Evento de seguridad: %s (nivel %s)", 
                     vehiculo_seguridad_tipo_amenaza_a_string(evento_seguridad.tipo_amenaza),
                     vehiculo_seguridad_nivel_amenaza_a_string(evento_seguridad.nivel_amenaza));
            
            // Procesar según el tipo de amenaza
            if (evento_seguridad.tipo_amenaza & AMENAZA_TIPO_TAMPER) {
                ESP_LOGE(TAG, "TAMPER detectado - activando medidas de emergencia");
                vehiculo_cambiar_estado(VEHICULO_ESTADO_ALARMA_ACTIVA);
            }
            else if (evento_seguridad.nivel_amenaza >= AMENAZA_NIVEL_ALTA) {
                ESP_LOGW(TAG, "Amenaza de nivel alto - aumentando seguridad");
                vehiculo_cambiar_estado(VEHICULO_ESTADO_ALERTA);
            }
        }
        
        // Esperar eventos del sistema
        eventos = xEventGroupWaitBits(g_vehiculo_app->eventos_sistema,
                                     EVT_COMANDO_RECIBIDO | EVT_TAMPER_DETECTADO | 
                                     EVT_SISTEMA_CRITICO | EVT_COMUNICACION_PERDIDA,
                                     pdTRUE, pdFALSE, 
                                     pdMS_TO_TICKS(500));
        
        if (eventos & EVT_COMANDO_RECIBIDO) {
            // Procesar comando recibido desde comunicación
            if (xQueueReceive(g_vehiculo_app->cola_comandos, &comando, 0) == pdTRUE) {
                ESP_LOGI(TAG, "Procesando comando: 0x%02X", comando);
                vehiculo_procesar_comando(comando, NULL, 0);
            }
        }
        
        if (eventos & EVT_TAMPER_DETECTADO) {
            ESP_LOGW(TAG, "TAMPER detectado del sistema principal");
            vehiculo_seguridad_reportar_amenaza(AMENAZA_TIPO_TAMPER,
                                              AMENAZA_NIVEL_CRITICA, 0);
        }
        
        if (eventos & EVT_COMUNICACION_PERDIDA) {
            ESP_LOGW(TAG, "Comunicación perdida - verificando jamming");
            if (vehiculo_seguridad_detectar_jamming()) {
                ESP_LOGW(TAG, "Jamming detectado");
            }
        }
        
        if (eventos & EVT_SISTEMA_CRITICO) {
            ESP_LOGE(TAG, "Estado crítico del sistema");
            vehiculo_seguridad_reportar_amenaza(AMENAZA_TIPO_INTRUSION_FISICA,
                                              AMENAZA_NIVEL_CRITICA, 0);
        }
    }
}

/**
 * @brief Tarea de comunicación LoRa/BLE (CPU0)
 * Maneja protocolos de comunicación con la llave
 */
void vehiculo_task_comunicacion(void* pvParameters)
{
    ESP_LOGI(TAG, "Iniciando tarea de comunicación (CPU0)");
    
    esp_task_wdt_add(NULL);
    
    // TODO: Inicializar drivers SX1276 y BLE
    
    while (1) {
        esp_task_wdt_reset();
        
        // TODO: Implementar recepción LoRa
        // TODO: Implementar manejo BLE para pareado
        // TODO: Procesar mensajes entrantes
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/**
 * @brief Tarea de monitoreo de sensores (CPU0)
 * Lee estado de todos los sensores del vehículo
 */
void vehiculo_task_sensores(void* pvParameters)
{
    ESP_LOGI(TAG, "Iniciando tarea de sensores (CPU0)");
    
    esp_task_wdt_add(NULL);
    
    // Inicializar sistema de sensores
    ESP_ERROR_CHECK(vehiculo_sensores_init());
    ESP_ERROR_CHECK(vehiculo_sensores_iniciar_monitoreo());
    
    sensor_evento_t evento;
    TickType_t ultimo_reporte = xTaskGetTickCount();
    
    while (1) {
        esp_task_wdt_reset();
        
        // Procesar eventos de sensores 
        if (vehiculo_sensores_obtener_evento(&evento, pdMS_TO_TICKS(100)) == ESP_OK) {
            ESP_LOGI(TAG, "Evento sensor procesado: tipo=%d", evento.tipo_evento);
        }
        
        // Reporte periódico de estado simplificado
        if (xTaskGetTickCount() - ultimo_reporte > pdMS_TO_TICKS(30000)) {
            ESP_LOGI(TAG, "Estado sensores - Funcionamiento normal");
            ultimo_reporte = xTaskGetTickCount();
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/**
 * @brief Tarea de control de actuadores (CPU1)
 * Controla relés, buzzer y LEDs de estado
 */
void vehiculo_task_actuadores(void* pvParameters)
{
    ESP_LOGI(TAG, "Iniciando tarea de actuadores (CPU1)");
    
    esp_task_wdt_add(NULL);
    
    // Inicializar sistema de actuadores
    ESP_ERROR_CHECK(vehiculo_actuadores_init());
    
    TickType_t ultimo_heartbeat = xTaskGetTickCount();
    
    while (1) {
        esp_task_wdt_reset();
        
        // Control simplificado basado en estado del sistema
        switch (g_vehiculo_app->estado_actual) {
            case VEHICULO_ESTADO_DESARMADO:
                ESP_LOGD(TAG, "Sistema desarmado");
                break;
                
            case VEHICULO_ESTADO_ARMADO:
                ESP_LOGD(TAG, "Sistema armado");
                break;
                
            case VEHICULO_ESTADO_ALERTA:
                ESP_LOGD(TAG, "Sistema en alerta");
                break;
                
            case VEHICULO_ESTADO_ALARMA_ACTIVA:
                ESP_LOGW(TAG, "Alarma activa");
                break;
                
            default:
                break;
        }
        
        // Heartbeat cada 30 segundos
        if (xTaskGetTickCount() - ultimo_heartbeat > pdMS_TO_TICKS(30000)) {
            ESP_LOGI(TAG, "Actuadores - Estado: OK");
            ultimo_heartbeat = xTaskGetTickCount();
        }
        
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/**
 * @brief Tarea de monitoreo del sistema (CPU0)
 * Supervisa salud del sistema y telemetría
 */
void vehiculo_task_monitor_sistema(void* pvParameters)
{
    ESP_LOGI(TAG, "Iniciando tarea de monitor del sistema (CPU0)");
    
    esp_task_wdt_add(NULL);
    
    EventBits_t eventos;
    TickType_t ultimo_heartbeat = 0;
    
    while (1) {
        esp_task_wdt_reset();
        
        eventos = xEventGroupWaitBits(g_vehiculo_app->eventos_sistema,
                                     EVT_HEARTBEAT | EVT_BATERIA_BAJA,
                                     pdTRUE, pdFALSE,
                                     pdMS_TO_TICKS(5000));
        
        if (eventos & EVT_HEARTBEAT) {
            ultimo_heartbeat = xTaskGetTickCount();
            ESP_LOGI(TAG, "Heartbeat - Sistema operacional");
            
            // TODO: Enviar heartbeat a servidor si está configurado
        }
        
        if (eventos & EVT_BATERIA_BAJA) {
            ESP_LOGW(TAG, "Batería baja detectada: %dmV", 
                     g_vehiculo_app->estado_sensores.voltaje_bateria_mv);
            
            // TODO: Enviar alerta de batería baja
        }
        
        // Verificar salud del sistema
        TickType_t tiempo_actual = xTaskGetTickCount();
        if ((tiempo_actual - ultimo_heartbeat) > pdMS_TO_TICKS(INTERVALO_HEARTBEAT_MS * 2)) {
            ESP_LOGW(TAG, "Sistema sin heartbeat por período extendido");
        }
        
        // Log de estado periódico
        if ((tiempo_actual % pdMS_TO_TICKS(30000)) == 0) {  // Cada 30 segundos
            ESP_LOGI(TAG, "Estado: %d, Armado: %s, Batería: %" PRIu32 "mV", 
                     g_vehiculo_app->estado_actual,
                     g_vehiculo_app->sistema_armado ? "SÍ" : "NO",
                     g_vehiculo_app->estado_sensores.voltaje_bateria_mv);
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ============================================================================
// FUNCIONES PÚBLICAS PRINCIPALES
// ============================================================================

esp_err_t vehiculo_app_init(void)
{
    esp_err_t ret = ESP_OK;
    
    ESP_LOGI(TAG, "Inicializando aplicación vehicular v%s", VEHICULO_VERSION);
    
    // Alocar estructura principal
    g_vehiculo_app = calloc(1, sizeof(vehiculo_app_t));
    if (!g_vehiculo_app) {
        ESP_LOGE(TAG, "Error al alocar memoria para aplicación");
        return ESP_ERR_NO_MEM;
    }
    
    // Inicializar configuración por defecto - usando tipos correctos
    g_vehiculo_app->config_actuadores = (actuador_config_simplificado_t){
        .pin_rele1 = CONFIG_GPIO_RELE_SIRENA,
        .pin_rele2 = CONFIG_GPIO_RELE_LUCES, 
        .pin_rele3 = CONFIG_GPIO_RELE_BLOQUEO,
        .pin_rele4 = CONFIG_GPIO_RELE_GPS,
        .pin_buzzer = CONFIG_GPIO_BUZZER,
        .pin_led_estado = CONFIG_GPIO_LED_ESTADO,
        .tiempo_activacion_max_ms = 120000
    };
    
    g_vehiculo_app->config_sensores = (sensor_config_t){
        .pin_puerta_conductor = CONFIG_GPIO_PUERTA_CONDUCTOR,
        .pin_puerta_pasajero = CONFIG_GPIO_PUERTA_PASAJERO,
        .pin_capo = CONFIG_GPIO_CAPO,
        .pin_baul = CONFIG_GPIO_BAUL,
        .pin_movimiento_pir = CONFIG_GPIO_PIR,
        .pin_shock_sensor = CONFIG_GPIO_SHOCK,
        .pin_bateria_monitor = CONFIG_GPIO_ADC_BATERIA,
        .adc_bateria = CONFIG_ADC_CANAL_BATERIA,
        .pin_tamper_caja = CONFIG_GPIO_TAMPER,
        .umbral_bateria_baja_mv = 11000,
        .sensibilidad_shock = 50
    };
    
    // Estado inicial del sistema
    g_vehiculo_app->estado_actual = VEHICULO_ESTADO_DESARMADO;
    g_vehiculo_app->estado_anterior = VEHICULO_ESTADO_DESARMADO;
    g_vehiculo_app->sistema_armado = false;
    g_vehiculo_app->alarma_activa = false;
    g_vehiculo_app->modo_panico = false;
    
    // Inicializar NVS
    ret = vehiculo_init_nvs();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error inicializando NVS");
        goto error;
    }
    
    // Cargar configuración persistente
    ret = vehiculo_cargar_configuracion();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Advertencia cargando configuración");
    }
    
    // Crear objetos de sincronización FreeRTOS
    g_vehiculo_app->cola_comandos = xQueueCreate(10, sizeof(vehiculo_comando_t));
    g_vehiculo_app->cola_eventos = xQueueCreate(20, sizeof(vehiculo_eventos_t));
    g_vehiculo_app->eventos_sistema = xEventGroupCreate();
    g_vehiculo_app->mutex_estado = xSemaphoreCreateMutex();
    
    if (!g_vehiculo_app->cola_comandos || !g_vehiculo_app->cola_eventos ||
        !g_vehiculo_app->eventos_sistema || !g_vehiculo_app->mutex_estado) {
        ESP_LOGE(TAG, "Error creando objetos FreeRTOS");
        ret = ESP_ERR_NO_MEM;
        goto error;
    }
    
    // Inicializar hardware
    ret = vehiculo_init_gpio_actuadores();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error inicializando GPIO actuadores");
        goto error;
    }
    
    ret = vehiculo_init_gpio_sensores();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error inicializando GPIO sensores");
        goto error;
    }
    
    // Crear timers
    esp_timer_create_args_t timer_args = {
        .callback = vehiculo_heartbeat_callback,
        .name = "heartbeat_timer"
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &g_vehiculo_app->timer_heartbeat));
    
    timer_args.callback = vehiculo_timeout_autenticacion_callback;
    timer_args.name = "auth_timeout_timer";
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &g_vehiculo_app->timer_autenticacion));
    
    // Inicializar componentes compartidos - simplificado
    ret = protocolo_init();  // Función base disponible
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error inicializando protocolo");
        goto error;
    }
    
    ret = cripto_init();     // Función base disponible  
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error inicializando utilidades criptográficas");
        goto error;
    }
    
    // Inicializar componentes específicos del vehículo
    ret = vehiculo_actuadores_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error inicializando actuadores del vehículo");
        goto error;
    }
    
    ret = vehiculo_sensores_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error inicializando sensores del vehículo");
        goto error;
    }
    
    ret = vehiculo_seguridad_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error inicializando seguridad del vehículo");
        goto error;
    }
    
    ESP_LOGI(TAG, "Aplicación vehicular inicializada correctamente");
    return ESP_OK;
    
error:
    if (g_vehiculo_app) {
        // Limpiar recursos parcialmente asignados
        if (g_vehiculo_app->cola_comandos) vQueueDelete(g_vehiculo_app->cola_comandos);
        if (g_vehiculo_app->cola_eventos) vQueueDelete(g_vehiculo_app->cola_eventos);
        if (g_vehiculo_app->eventos_sistema) vEventGroupDelete(g_vehiculo_app->eventos_sistema);
        if (g_vehiculo_app->mutex_estado) vSemaphoreDelete(g_vehiculo_app->mutex_estado);
        
        free(g_vehiculo_app);
        g_vehiculo_app = NULL;
    }
    
    // Limpiar recursos ADC
    if (g_adc1_handle) {
        adc_oneshot_del_unit(g_adc1_handle);
        g_adc1_handle = NULL;
    }
    
    return ret;
}

esp_err_t vehiculo_app_run(void)
{
    if (!g_vehiculo_app) {
        ESP_LOGE(TAG, "Aplicación no inicializada");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Iniciando aplicación vehicular");
    
    // Iniciar timer de heartbeat
    ESP_ERROR_CHECK(esp_timer_start_periodic(g_vehiculo_app->timer_heartbeat, 
                                            INTERVALO_HEARTBEAT_MS * 1000));
    
    // Crear tareas FreeRTOS con afinidad de CPU específica
    BaseType_t ret;
    
    // CPU1: Tareas críticas de seguridad y actuadores
    ret = xTaskCreatePinnedToCore(vehiculo_task_seguridad, "vehiculo_seguridad",
                                 STACK_SEGURIDAD, NULL, PRIO_SEGURIDAD_TASK,
                                 NULL, 1);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Error creando tarea de seguridad");
        return ESP_FAIL;
    }
    
    ret = xTaskCreatePinnedToCore(vehiculo_task_actuadores, "vehiculo_actuadores",
                                 STACK_ACTUADORES, NULL, PRIO_ACTUADORES_TASK,
                                 NULL, 1);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Error creando tarea de actuadores");
        return ESP_FAIL;
    }
    
    // CPU0: Tareas de comunicación, sensores y monitoreo
    ret = xTaskCreatePinnedToCore(vehiculo_task_comunicacion, "vehiculo_comunicacion",
                                 STACK_COMUNICACION, NULL, PRIO_COMUNICACION_TASK,
                                 NULL, 0);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Error creando tarea de comunicación");
        return ESP_FAIL;
    }
    
    ret = xTaskCreatePinnedToCore(vehiculo_task_sensores, "vehiculo_sensores",
                                 STACK_SENSORES, NULL, PRIO_SENSORES_TASK,
                                 NULL, 0);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Error creando tarea de sensores");
        return ESP_FAIL;
    }
    
    ret = xTaskCreatePinnedToCore(vehiculo_task_monitor_sistema, "vehiculo_monitor",
                                 STACK_MONITOR, NULL, PRIO_MONITOR_TASK,
                                 NULL, 0);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Error creando tarea de monitor");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Todas las tareas iniciadas correctamente");
    ESP_LOGI(TAG, "Sistema vehicular operacional");
    
    // La función no debería retornar en operación normal
    // Las tareas manejan toda la funcionalidad
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        
        // Verificación periódica de integridad del sistema
        if (!vehiculo_verificar_integridad()) {
            ESP_LOGE(TAG, "Falla de integridad del sistema");
            vehiculo_reinicio_seguro("Falla de integridad");
        }
    }
    
    return ESP_OK;
}

esp_err_t vehiculo_procesar_comando(vehiculo_comando_t comando, 
                                  const uint8_t* datos, 
                                  size_t tamaño)
{
    if (!g_vehiculo_app) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Procesando comando: 0x%02X", comando);
    
    esp_err_t ret = ESP_OK;
    
    switch (comando) {
        case CMD_ARMAR_SISTEMA:
            ret = vehiculo_armar_sistema(true);
            break;
            
        case CMD_DESARMAR_SISTEMA:
            ret = vehiculo_armar_sistema(false);
            break;
            
        case CMD_PANICO_VEHICULO:
            ret = vehiculo_activar_panico();
            break;
            
        case CMD_LOCALIZAR:
            // Activar señales de localización por tiempo limitado
            gpio_set_level(g_vehiculo_app->config_actuadores.pin_buzzer, 1);
            gpio_set_level(g_vehiculo_app->config_actuadores.pin_rele2, 1);
            vTaskDelay(pdMS_TO_TICKS(3000));
            gpio_set_level(g_vehiculo_app->config_actuadores.pin_buzzer, 0);
            gpio_set_level(g_vehiculo_app->config_actuadores.pin_rele2, 0);
            break;
            
        case CMD_STATUS:
            // TODO: Preparar y enviar estado del sistema
            ESP_LOGI(TAG, "Estado solicitado - preparando respuesta");
            break;
            
        case CMD_RESET_ALARMA:
            if (g_vehiculo_app->estado_actual == VEHICULO_ESTADO_ALARMA_ACTIVA) {
                vehiculo_cambiar_estado(VEHICULO_ESTADO_ARMADO);
            }
            break;
            
        default:
            ESP_LOGW(TAG, "Comando no reconocido: 0x%02X", comando);
            ret = ESP_ERR_NOT_SUPPORTED;
            break;
    }
    
    return ret;
}

esp_err_t vehiculo_cambiar_estado(vehiculo_estado_t nuevo_estado)
{
    if (!g_vehiculo_app) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(g_vehiculo_app->mutex_estado, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    vehiculo_estado_t estado_previo = g_vehiculo_app->estado_actual;
    g_vehiculo_app->estado_anterior = estado_previo;
    g_vehiculo_app->estado_actual = nuevo_estado;
    
    ESP_LOGI(TAG, "Cambio de estado: %d -> %d", estado_previo, nuevo_estado);
    
    // Acciones específicas según el nuevo estado
    switch (nuevo_estado) {
        case VEHICULO_ESTADO_DESARMADO:
            g_vehiculo_app->sistema_armado = false;
            g_vehiculo_app->alarma_activa = false;
            break;
            
        case VEHICULO_ESTADO_ARMADO:
            g_vehiculo_app->sistema_armado = true;
            g_vehiculo_app->alarma_activa = false;
            break;
            
        case VEHICULO_ESTADO_ALERTA:
            // Dar tiempo para validación antes de activar alarma
            esp_timer_start_once(g_vehiculo_app->timer_autenticacion, 
                               TIMEOUT_AUTENTICACION_MS * 1000);
            break;
            
        case VEHICULO_ESTADO_ALARMA_ACTIVA:
            g_vehiculo_app->alarma_activa = true;
            g_vehiculo_app->detecciones_intrusion++;
            break;
            
        case VEHICULO_ESTADO_PANICO:
            g_vehiculo_app->modo_panico = true;
            g_vehiculo_app->alarma_activa = true;
            xEventGroupSetBits(g_vehiculo_app->eventos_sistema, EVT_EMERGENCIA);
            break;
            
        case VEHICULO_ESTADO_ERROR_CRITICO:
            xEventGroupSetBits(g_vehiculo_app->eventos_sistema, EVT_SISTEMA_CRITICO);
            break;
            
        default:
            break;
    }
    
    xSemaphoreGive(g_vehiculo_app->mutex_estado);
    return ESP_OK;
}

vehiculo_estado_t vehiculo_obtener_estado(void)
{
    if (!g_vehiculo_app) {
        return VEHICULO_ESTADO_ERROR_CRITICO;
    }
    
    return g_vehiculo_app->estado_actual;
}

esp_err_t vehiculo_armar_sistema(bool armar)
{
    ESP_LOGI(TAG, "%s sistema de alarma", armar ? "Armando" : "Desarmando");
    
    if (armar) {
        return vehiculo_cambiar_estado(VEHICULO_ESTADO_ARMADO);
    } else {
        return vehiculo_cambiar_estado(VEHICULO_ESTADO_DESARMADO);
    }
}

esp_err_t vehiculo_activar_panico(void)
{
    ESP_LOGW(TAG, "MODO PÁNICO ACTIVADO");
    return vehiculo_cambiar_estado(VEHICULO_ESTADO_PANICO);
}

esp_err_t vehiculo_manejar_emergencia(vehiculo_eventos_t tipo_emergencia)
{
    ESP_LOGE(TAG, "Manejando emergencia: 0x%02X", tipo_emergencia);
    
    switch (tipo_emergencia) {
        case EVT_TAMPER_DETECTADO:
            // Activar alarma inmediatamente sin validación
            vehiculo_cambiar_estado(VEHICULO_ESTADO_ALARMA_ACTIVA);
            break;
            
        case EVT_SISTEMA_CRITICO:
            // TODO: Enviar alerta crítica, preparar coredump
            ESP_LOGE(TAG, "Sistema en estado crítico - requiere intervención");
            break;
            
        default:
            break;
    }
    
    return ESP_OK;
}

vehiculo_app_t* vehiculo_get_app_instance(void)
{
    return g_vehiculo_app;
}

// ============================================================================
// FUNCIONES AUXILIARES SIMPLIFICADAS
// ============================================================================

/**
 * @brief Callback para reportar estado de sensores
 */
static void __attribute__((unused)) vehiculo_reportar_estado_sensor_callback(int sensor_id, int tipo_evento, bool estado)
{
    ESP_LOGI(TAG, "Callback sensor: ID=%d, Evento=%d, Estado=%d", sensor_id, tipo_evento, estado);
    
    // Los eventos críticos se manejan directamente en la tarea de sensores
    // Este callback puede usarse para estadísticas o logging adicional
}

/**
 * @brief Función para reinicio seguro del sistema
 */
void vehiculo_reinicio_seguro(const char* motivo)
{
    ESP_LOGE(TAG, "Reinicio seguro solicitado: %s", motivo);
    
    vTaskDelay(pdMS_TO_TICKS(1000));  // Dar tiempo para logging
    esp_restart();
}

bool vehiculo_verificar_integridad(void)
{
    if (!g_vehiculo_app) {
        return false;
    }
    
    // Verificaciones básicas de integridad
    if (g_vehiculo_app->cola_comandos == NULL ||
        g_vehiculo_app->eventos_sistema == NULL ||
        g_vehiculo_app->mutex_estado == NULL) {
        return false;
    }
    
    // TODO: Verificar integridad de memoria, stack overflow, etc.
    
    return true;
}

size_t vehiculo_generar_reporte_estado(char* buffer, size_t tamaño)
{
    if (!buffer || tamaño == 0 || !g_vehiculo_app) {
        return 0;
    }
    
    return snprintf(buffer, tamaño,
        "Estado: %d\n"
        "Armado: %s\n"
        "Alarma: %s\n"
        "Batería: %" PRIu32 "mV\n"
        "Puertas: C:%d P:%d\n"
        "Capó: %d Baúl: %d\n"
        "Movimiento: %d\n"
        "Tamper: %d\n"
        "Intentos fallidos: %" PRIu32 "\n",
        g_vehiculo_app->estado_actual,
        g_vehiculo_app->sistema_armado ? "SÍ" : "NO",
        g_vehiculo_app->alarma_activa ? "SÍ" : "NO",
        g_vehiculo_app->estado_sensores.voltaje_bateria_mv,
        g_vehiculo_app->estado_sensores.puerta_conductor,
        g_vehiculo_app->estado_sensores.puerta_pasajero,
        g_vehiculo_app->estado_sensores.capo,
        g_vehiculo_app->estado_sensores.baul,
        g_vehiculo_app->estado_sensores.movimiento_detectado,
        g_vehiculo_app->estado_sensores.tamper_detectado,
        g_vehiculo_app->intentos_autenticacion_fallidos
    );
}
