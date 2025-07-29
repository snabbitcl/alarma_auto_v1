#include "app_llave.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "esp_task_wdt.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "protocolo_seguro.h"

static const char* TAG = "APP_LLAVE";

// Handles para la nueva API de ADC
static adc_oneshot_unit_handle_t g_adc1_handle = NULL;
static adc_cali_handle_t g_adc_cali_handle = NULL;

// Handles de tareas
TaskHandle_t task_gui_handle = NULL;
TaskHandle_t task_ble_handle = NULL;
TaskHandle_t task_lora_tx_handle = NULL;
TaskHandle_t task_lora_rx_handle = NULL;
TaskHandle_t task_power_handle = NULL;
TaskHandle_t task_security_handle = NULL;

// Colas y semáforos
QueueHandle_t cola_eventos_principales = NULL;
QueueHandle_t cola_comandos_lora = NULL;
SemaphoreHandle_t mutex_display = NULL;
SemaphoreHandle_t mutex_lora = NULL;

// Estado global de la aplicación
static struct {
    llave_estado_t estado_actual;
    uint32_t tiempo_inicio;
    uint32_t comandos_enviados;
    int8_t ultimo_rssi;
    bool vehiculo_conectado;
    uint8_t nivel_bateria;
} app_estado = {
    .estado_actual = LLAVE_ESTADO_ARRANQUE,
    .tiempo_inicio = 0,
    .comandos_enviados = 0,
    .ultimo_rssi = -127,
    .vehiculo_conectado = false,
    .nivel_bateria = 100
};

esp_err_t app_llave_init(void) {
    ESP_LOGI(TAG, "Inicializando aplicación llave...");
    
    // Crear colas
    cola_eventos_principales = xQueueCreate(20, sizeof(llave_evento_msg_t));
    cola_comandos_lora = xQueueCreate(10, sizeof(mensaje_seguro_t));
    
    if (!cola_eventos_principales || !cola_comandos_lora) {
        ESP_LOGE(TAG, "Error creando colas");
        return ESP_ERR_NO_MEM;
    }
    
    // Crear semáforos
    mutex_display = xSemaphoreCreateMutex();
    mutex_lora = xSemaphoreCreateMutex();
    
    if (!mutex_display || !mutex_lora) {
        ESP_LOGE(TAG, "Error creando semáforos");
        return ESP_ERR_NO_MEM;
    }
    
    // Configurar pines GPIO
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << PIN_LED_STATUS),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    gpio_config(&io_conf);
    
    // Configurar botones
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << PIN_BOTON_1) | (1ULL << PIN_BOTON_2) | 
                          (1ULL << PIN_BOTON_3) | (1ULL << PIN_BOTON_4);
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);
    
    // Configurar ADC para batería - Nueva API
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
    ret = adc_oneshot_config_channel(g_adc1_handle, ADC_CHANNEL_3, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error configurando canal ADC: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Inicializar calibración ADC con nueva API
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    
    ret = adc_cali_create_scheme_curve_fitting(&cali_config, &g_adc_cali_handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibración ADC inicializada con curve fitting");
    } else {
        ESP_LOGW(TAG, "Calibración ADC no disponible, usando valores sin calibrar");
        g_adc_cali_handle = NULL;
    }
    
    // TODO: Inicializar drivers SPI, TFT, LoRa, BLE
    
    app_estado.tiempo_inicio = esp_timer_get_time() / 1000;
    app_estado.estado_actual = LLAVE_ESTADO_BLOQUEADA;
    
    ESP_LOGI(TAG, "Aplicación llave inicializada");
    return ESP_OK;
}

esp_err_t app_llave_start_tasks(void) {
    ESP_LOGI(TAG, "Arrancando tareas...");
    
    // Crear tarea GUI (núcleo 0)
    BaseType_t ret = xTaskCreatePinnedToCore(
        tarea_gui_principal,
        "GUI_Task",
        STACK_GUI_TASK,
        NULL,
        PRIORIDAD_GUI_TASK,
        &task_gui_handle,
        0
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Error creando tarea GUI");
        return ESP_FAIL;
    }
    
    // Crear tarea BLE (núcleo 0)
    ret = xTaskCreatePinnedToCore(
        tarea_ble_cliente,
        "BLE_Task",
        STACK_BLE_TASK,
        NULL,
        PRIORIDAD_BLE_TASK,
        &task_ble_handle,
        0
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Error creando tarea BLE");
        return ESP_FAIL;
    }
    
    // Crear tarea LoRa (núcleo 1)
    ret = xTaskCreatePinnedToCore(
        tarea_lora_transceptor,
        "LoRa_Task",
        STACK_LORA_TASK,
        NULL,
        PRIORIDAD_LORA_TX_TASK,
        &task_lora_tx_handle,
        1
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Error creando tarea LoRa");
        return ESP_FAIL;
    }
    
    // Crear tarea power management (núcleo 0)
    ret = xTaskCreatePinnedToCore(
        tarea_gestion_energia,
        "Power_Task",
        STACK_POWER_TASK,
        NULL,
        PRIORIDAD_POWER_TASK,
        &task_power_handle,
        0
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Error creando tarea Power");
        return ESP_FAIL;
    }
    
    // Crear tarea seguridad (núcleo 1)
    ret = xTaskCreatePinnedToCore(
        tarea_monitor_seguridad,
        "Security_Task",
        STACK_SECURITY_TASK,
        NULL,
        PRIORIDAD_SECURITY_TASK,
        &task_security_handle,
        1
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Error creando tarea Security");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Todas las tareas arrancadas");
    return ESP_OK;
}

void app_llave_main_loop(void) {
    ESP_LOGI(TAG, "Entrando en bucle principal");
    
    llave_evento_msg_t evento;
    TickType_t timeout = pdMS_TO_TICKS(1000); // 1 segundo
    
    while (1) {
        // Esperar eventos con timeout
        if (xQueueReceive(cola_eventos_principales, &evento, timeout) == pdTRUE) {
            // Procesar evento
            esp_err_t ret = procesar_evento(&evento);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Error procesando evento: %s", esp_err_to_name(ret));
            }
        } else {
            // Timeout - tareas de mantenimiento
            if (app_estado.estado_actual == LLAVE_ESTADO_ACTIVA) {
                // Verificar si debemos entrar en sleep
                // TODO: Implementar lógica de inactividad
            }
        }
        
        // Watchdog reset
        esp_task_wdt_reset();
    }
}

esp_err_t procesar_evento(llave_evento_msg_t* evento) {
    if (!evento) return ESP_ERR_INVALID_ARG;
    
    ESP_LOGI(TAG, "Procesando evento tipo: %d", evento->tipo);
    
    switch (evento->tipo) {
        case EVENTO_BOTON_PRESIONADO:
            // TODO: Manejar botones
            break;
            
        case EVENTO_COMANDO_VEHICULO:
            // TODO: Enviar comando por LoRa
            break;
            
        case EVENTO_RESPUESTA_RECIBIDA:
            // TODO: Procesar respuesta del vehículo
            break;
            
        case EVENTO_BATERIA_BAJA:
            cambiar_estado(LLAVE_ESTADO_SLEEP);
            break;
            
        case EVENTO_SLEEP_REQUEST:
            cambiar_estado(LLAVE_ESTADO_SLEEP);
            break;
            
        default:
            ESP_LOGW(TAG, "Evento no manejado: %d", evento->tipo);
            break;
    }
    
    return ESP_OK;
}

llave_estado_t obtener_estado_actual(void) {
    return app_estado.estado_actual;
}

esp_err_t cambiar_estado(llave_estado_t nuevo_estado) {
    if (!puede_cambiar_estado(app_estado.estado_actual, nuevo_estado)) {
        return ESP_ERR_INVALID_STATE;
    }
    
    llave_estado_t estado_anterior = app_estado.estado_actual;
    app_estado.estado_actual = nuevo_estado;
    
    ESP_LOGI(TAG, "Cambio de estado: %d -> %d", estado_anterior, nuevo_estado);
    
    // Acciones según el nuevo estado
    switch (nuevo_estado) {
        case LLAVE_ESTADO_SLEEP:
            // Preparar para deep sleep
            esp_sleep_enable_ext0_wakeup(PIN_BOTON_1, 0);
            esp_deep_sleep_start();
            break;
            
        default:
            break;
    }
    
    return ESP_OK;
}

bool puede_cambiar_estado(llave_estado_t desde, llave_estado_t hacia) {
    // Matriz de transiciones válidas
    switch (desde) {
        case LLAVE_ESTADO_ARRANQUE:
            return (hacia == LLAVE_ESTADO_BLOQUEADA || hacia == LLAVE_ESTADO_ERROR);
        case LLAVE_ESTADO_BLOQUEADA:
            return (hacia == LLAVE_ESTADO_ACTIVA || hacia == LLAVE_ESTADO_SLEEP);
        case LLAVE_ESTADO_ACTIVA:
            return true; // Puede ir a cualquier estado
        case LLAVE_ESTADO_SLEEP:
            return (hacia == LLAVE_ESTADO_BLOQUEADA);
        default:
            return false;
    }
}

uint32_t obtener_tiempo_uptime(void) {
    return (esp_timer_get_time() / 1000) - app_estado.tiempo_inicio;
}

uint8_t obtener_nivel_bateria(void) {
    // Leer ADC y convertir a porcentaje con nueva API
    int adc_raw = 0;
    esp_err_t ret = adc_oneshot_read(g_adc1_handle, ADC_CHANNEL_3, &adc_raw);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Error leyendo ADC: %s", esp_err_to_name(ret));
        return app_estado.nivel_bateria;  // Retornar último valor válido
    }
    
    // Convertir a mV usando calibración si está disponible
    uint32_t voltaje_mv = 0;
    if (g_adc_cali_handle != NULL) {
        ret = adc_cali_raw_to_voltage(g_adc_cali_handle, adc_raw, (int*)&voltaje_mv);
        if (ret != ESP_OK) {
            // Si falla la calibración, usar conversión aproximada
            voltaje_mv = (adc_raw * 3300) / 4095;  // Para 12 bits y 3.3V
        }
    } else {
        // Sin calibración, conversión aproximada
        voltaje_mv = (adc_raw * 3300) / 4095;  // Para 12 bits y 3.3V
    }
    
    // TODO: Calibrar conversión a voltaje y porcentaje según divisor de tensión
    // Por ahora mantener valor actual hasta calibración completa
    return app_estado.nivel_bateria;
}

esp_err_t enviar_evento(llave_evento_t tipo, uint32_t param1, uint32_t param2, void* data) {
    llave_evento_msg_t evento = {
        .tipo = tipo,
        .param1 = param1,
        .param2 = param2,
        .data = data
    };
    
    BaseType_t ret = xQueueSend(cola_eventos_principales, &evento, pdMS_TO_TICKS(100));
    return (ret == pdTRUE) ? ESP_OK : ESP_FAIL;
}

// Implementaciones básicas de tareas (a completar)
void tarea_gui_principal(void* pvParameters) {
    ESP_LOGI(TAG, "Tarea GUI iniciada");
    while (1) {
        // TODO: Implementar lógica GUI con LVGL
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void tarea_ble_cliente(void* pvParameters) {
    ESP_LOGI(TAG, "Tarea BLE iniciada");
    while (1) {
        // TODO: Implementar cliente BLE
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void tarea_lora_transceptor(void* pvParameters) {
    ESP_LOGI(TAG, "Tarea LoRa iniciada");
    while (1) {
        // TODO: Implementar transceptor LoRa
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void tarea_gestion_energia(void* pvParameters) {
    ESP_LOGI(TAG, "Tarea gestión energía iniciada");
    while (1) {
        // Monitorear nivel de batería
        app_estado.nivel_bateria = obtener_nivel_bateria();
        
        if (app_estado.nivel_bateria < 10) {
            enviar_evento(EVENTO_BATERIA_BAJA, 0, 0, NULL);
        }
        
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void tarea_monitor_seguridad(void* pvParameters) {
    ESP_LOGI(TAG, "Tarea monitor seguridad iniciada");
    while (1) {
        // TODO: Implementar monitoreo de seguridad
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Función para limpiar recursos ADC
esp_err_t app_llave_deinit_adc(void) {
    esp_err_t ret = ESP_OK;
    
    // Liberar recursos ADC con nueva API
    if (g_adc_cali_handle) {
        ret = adc_cali_delete_scheme_curve_fitting(g_adc_cali_handle);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Error liberando calibración ADC: %s", esp_err_to_name(ret));
        }
        g_adc_cali_handle = NULL;
    }
    
    if (g_adc1_handle) {
        ret = adc_oneshot_del_unit(g_adc1_handle);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Error liberando unidad ADC1: %s", esp_err_to_name(ret));
        }
        g_adc1_handle = NULL;
    }
    
    ESP_LOGI(TAG, "Recursos ADC liberados");
    return ret;
}
