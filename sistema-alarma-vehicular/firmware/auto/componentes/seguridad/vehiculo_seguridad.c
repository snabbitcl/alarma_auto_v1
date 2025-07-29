/**
 * @file vehiculo_seguridad.c
 * @brief Implementación del sistema de seguridad avanzado del vehículo
 * 
 * Sistema multicapa de seguridad con detección de ataques sofisticados,
 * validación de proximidad y respuesta automática ante amenazas
 * 
 * @author Sistema Alarma Vehicular
 * @date 2024
 */

#include "vehiculo_seguridad.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <math.h>

// ============================================================================
// VARIABLES GLOBALES Y CONFIGURACIÓN
// ============================================================================

static vehiculo_seguridad_t* g_seguridad = NULL;
static const char* TAG = SEGURIDAD_TAG;

// Configuración por defecto del sistema de seguridad
static const seguridad_config_t SEGURIDAD_CONFIG_DEFAULT = {
    .seguridad_habilitada = true,
    .modo_paranoia = false,
    .max_intentos_autenticacion = SEGURIDAD_MAX_INTENTOS_AUTENTICACION,
    .tiempo_bloqueo_ms = SEGURIDAD_TIEMPO_BLOQUEO_MS,
    .tiempo_sesion_max_ms = SEGURIDAD_TIEMPO_SESION_MS,
    
    .config_proximidad = {
        .metodo = PROXIMIDAD_RSSI,
        .rssi_minimo = SEGURIDAD_RSSI_UMBRAL_LEJANO,
        .rssi_maximo = SEGURIDAD_RSSI_UMBRAL_CERCANO,
        .tiempo_vuelo_max_us = 5000,        // 5ms máximo para LoRa local
        .tolerancia_timing_us = 1000,       // 1ms de tolerancia
        .validacion_dual_radio = true,
        .canales_verificacion = {0, 1, 2}   // Canales de verificación
    },
    
    .anti_jamming_habilitado = true,
    .tiempo_deteccion_jamming_ms = SEGURIDAD_TIEMPO_SIN_COMUNICACION_MS,
    .canales_backup = {3, 4, 5},           // Canales de respaldo anti-jamming
    
    .respuesta_automatica = true,
    .umbral_alarma = AMENAZA_NIVEL_MEDIA,
    .umbral_panico = AMENAZA_NIVEL_CRITICA
};

// ============================================================================
// FUNCIONES PRIVADAS
// ============================================================================

/**
 * @brief Callback para timeout de sesión
 */
static void timeout_sesion_callback(void* arg)
{
    ESP_LOGW(TAG, "Timeout de sesión de seguridad");
    
    if (g_seguridad && g_seguridad->eventos_sistema) {
        // Marcar evento de timeout de sesión
        if (g_seguridad->sesion_actual.sesion_activa) {
            vehiculo_seguridad_cerrar_sesion("Timeout de sesión");
        }
    }
}

/**
 * @brief Callback para heartbeat de seguridad
 */
static void heartbeat_seguridad_callback(void* arg)
{
    if (!g_seguridad || !g_seguridad->inicializado) {
        return;
    }
    
    uint64_t tiempo_actual = esp_timer_get_time();
    
    // Verificar si hay sesión activa sin heartbeat
    if (g_seguridad->sesion_actual.sesion_activa) {
        uint64_t tiempo_sin_heartbeat = tiempo_actual - 
                                       g_seguridad->sesion_actual.timestamp_ultimo_heartbeat;
        
        if (tiempo_sin_heartbeat > (SEGURIDAD_INTERVALO_HEARTBEAT_MS * 1500)) {  // 1.5x timeout
            ESP_LOGW(TAG, "Sesión sin heartbeat por %llu ms", tiempo_sin_heartbeat / 1000);
            
            evento_seguridad_t evento = {
                .tipo_amenaza = AMENAZA_TIPO_JAMMING,
                .nivel_amenaza = AMENAZA_NIVEL_MEDIA,
                .timestamp = tiempo_actual,
                .rssi_asociado = 0,
                .datos_adicionales = (uint32_t)(tiempo_sin_heartbeat / 1000)
            };
            strcpy(evento.descripcion, "Sesión sin heartbeat");
            
            if (g_seguridad->cola_eventos_seguridad) {
                xQueueSend(g_seguridad->cola_eventos_seguridad, &evento, 0);
            }
        }
    }
}

/**
 * @brief Callback para detección anti-jamming
 */
static void anti_jamming_callback(void* arg)
{
    if (!g_seguridad || !g_seguridad->configuracion.anti_jamming_habilitado) {
        return;
    }
    
    // Verificar si hay comunicación reciente
    uint64_t tiempo_actual = esp_timer_get_time();
    uint64_t tiempo_sin_comunicacion = tiempo_actual - 
                                      g_seguridad->estadisticas.tiempo_ultima_autenticacion;
    
    if (tiempo_sin_comunicacion > (g_seguridad->configuracion.tiempo_deteccion_jamming_ms * 1000)) {
        ESP_LOGW(TAG, "Posible jamming detectado - %llu ms sin comunicación", 
                 tiempo_sin_comunicacion / 1000);
        
        vehiculo_seguridad_reportar_amenaza(AMENAZA_TIPO_JAMMING, 
                                          AMENAZA_NIVEL_ALTA, 
                                          (uint32_t)(tiempo_sin_comunicacion / 1000));
    }
}

/**
 * @brief Calcula el nivel de amenaza global basado en amenazas activas
 */
static amenaza_nivel_t calcular_nivel_amenaza_global(amenaza_tipo_t amenazas_activas)
{
    amenaza_nivel_t nivel_maximo = AMENAZA_NIVEL_NINGUNA;
    
    // Amenazas críticas
    if (amenazas_activas & (AMENAZA_TIPO_TAMPER | AMENAZA_TIPO_BRUTE_FORCE)) {
        nivel_maximo = AMENAZA_NIVEL_CRITICA;
    }
    // Amenazas altas
    else if (amenazas_activas & (AMENAZA_TIPO_RELAY_ATTACK | AMENAZA_TIPO_REPLAY_ATTACK)) {
        nivel_maximo = AMENAZA_NIVEL_ALTA;
    }
    // Amenazas medias
    else if (amenazas_activas & (AMENAZA_TIPO_JAMMING | AMENAZA_TIPO_TIEMPO_RESPUESTA)) {
        if (nivel_maximo < AMENAZA_NIVEL_MEDIA) {
            nivel_maximo = AMENAZA_NIVEL_MEDIA;
        }
    }
    // Amenazas bajas
    else if (amenazas_activas & (AMENAZA_TIPO_RSSI_ANOMALO | AMENAZA_TIPO_FRECUENCIA_ANOMALA)) {
        if (nivel_maximo < AMENAZA_NIVEL_BAJA) {
            nivel_maximo = AMENAZA_NIVEL_BAJA;
        }
    }
    
    return nivel_maximo;
}

/**
 * @brief Procesa eventos de seguridad y toma acciones automáticas
 */
static void procesar_respuesta_automatica(amenaza_tipo_t nueva_amenaza, 
                                         amenaza_nivel_t nivel_amenaza)
{
    if (!g_seguridad->configuracion.respuesta_automatica) {
        return;
    }
    
    ESP_LOGW(TAG, "Procesando respuesta automática para amenaza 0x%02X nivel %d", 
             nueva_amenaza, nivel_amenaza);
    
    // Respuesta inmediata para amenazas críticas
    if (nivel_amenaza >= AMENAZA_NIVEL_CRITICA) {
        ESP_LOGE(TAG, "AMENAZA CRÍTICA - Activando respuesta de emergencia");
        
        // Cerrar sesión inmediatamente
        vehiculo_seguridad_cerrar_sesion("Amenaza crítica detectada");
        
        // Bloquear sistema por tiempo extendido
        g_seguridad->estado_actual = SEGURIDAD_ESTADO_BLOQUEADO;
        
        // TODO: Activar medidas adicionales (alarma, bloqueo motor, etc.)
    }
    // Respuesta para amenazas altas
    else if (nivel_amenaza >= g_seguridad->configuracion.umbral_alarma) {
        ESP_LOGW(TAG, "Amenaza alta - Aumentando nivel de seguridad");
        
        // Reducir tiempo de sesión
        if (g_seguridad->timer_sesion) {
            esp_timer_stop(g_seguridad->timer_sesion);
            esp_timer_start_once(g_seguridad->timer_sesion, 
                               g_seguridad->configuracion.tiempo_sesion_max_ms * 500);  // 50% del tiempo
        }
        
        // Modo paranoia temporal
        g_seguridad->configuracion.modo_paranoia = true;
    }
}

/**
 * @brief Tarea principal de monitoreo de seguridad
 */
static void task_monitoreo_seguridad(void* pvParameters)
{
    ESP_LOGI(TAG, "Iniciando tarea de monitoreo de seguridad");
    
    evento_seguridad_t evento;
    
    while (g_seguridad && g_seguridad->monitoreo_activo) {
        
        // Procesar eventos de seguridad
        if (xQueueReceive(g_seguridad->cola_eventos_seguridad, &evento, 
                         pdMS_TO_TICKS(1000)) == pdTRUE) {
            
            ESP_LOGW(TAG, "Evento de seguridad: %s (nivel %d)", 
                     evento.descripcion, evento.nivel_amenaza);
            
            if (xSemaphoreTake(g_seguridad->mutex_seguridad, pdMS_TO_TICKS(1000)) == pdTRUE) {
                
                // Agregar amenaza a las activas
                g_seguridad->amenazas_detectadas |= evento.tipo_amenaza;
                g_seguridad->timestamp_ultima_amenaza = evento.timestamp;
                
                // Recalcular nivel de amenaza global
                amenaza_nivel_t nuevo_nivel = calcular_nivel_amenaza_global(g_seguridad->amenazas_detectadas);
                
                if (nuevo_nivel > g_seguridad->nivel_amenaza_global) {
                    g_seguridad->nivel_amenaza_global = nuevo_nivel;
                    ESP_LOGW(TAG, "Nivel de amenaza global actualizado a %d", nuevo_nivel);
                }
                
                // Procesar respuesta automática
                procesar_respuesta_automatica(evento.tipo_amenaza, evento.nivel_amenaza);
                
                xSemaphoreGive(g_seguridad->mutex_seguridad);
            }
        }
        
        // Verificaciones periódicas adicionales
        uint64_t tiempo_actual = esp_timer_get_time();
        
        // Limpiar amenazas antiguas (más de 5 minutos)
        if (tiempo_actual - g_seguridad->timestamp_ultima_amenaza > 300000000ULL) {  // 5 min
            if (xSemaphoreTake(g_seguridad->mutex_seguridad, pdMS_TO_TICKS(100)) == pdTRUE) {
                g_seguridad->amenazas_detectadas = 0;
                g_seguridad->nivel_amenaza_global = AMENAZA_NIVEL_NINGUNA;
                g_seguridad->configuracion.modo_paranoia = false;  // Salir de modo paranoia
                xSemaphoreGive(g_seguridad->mutex_seguridad);
            }
        }
    }
    
    ESP_LOGI(TAG, "Tarea de monitoreo de seguridad terminada");
    vTaskDelete(NULL);
}

// ============================================================================
// FUNCIONES PÚBLICAS
// ============================================================================

esp_err_t vehiculo_seguridad_init(void)
{
    if (g_seguridad != NULL) {
        ESP_LOGW(TAG, "Módulo de seguridad ya inicializado");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Inicializando módulo de seguridad del vehículo");
    
    // Alocar estructura principal
    g_seguridad = calloc(1, sizeof(vehiculo_seguridad_t));
    if (!g_seguridad) {
        ESP_LOGE(TAG, "Error asignando memoria para seguridad");
        return ESP_ERR_NO_MEM;
    }
    
    // Configuración inicial
    memcpy(&g_seguridad->configuracion, &SEGURIDAD_CONFIG_DEFAULT, 
           sizeof(seguridad_config_t));
    
    g_seguridad->estado_actual = SEGURIDAD_ESTADO_NO_AUTENTICADO;
    g_seguridad->nivel_amenaza_global = AMENAZA_NIVEL_NINGUNA;
    g_seguridad->amenazas_detectadas = 0;
    
    // Crear objetos de sincronización
    g_seguridad->mutex_seguridad = xSemaphoreCreateMutex();
    if (!g_seguridad->mutex_seguridad) {
        ESP_LOGE(TAG, "Error creando mutex de seguridad");
        free(g_seguridad);
        g_seguridad = NULL;
        return ESP_ERR_NO_MEM;
    }
    
    g_seguridad->cola_eventos_seguridad = xQueueCreate(15, sizeof(evento_seguridad_t));
    if (!g_seguridad->cola_eventos_seguridad) {
        ESP_LOGE(TAG, "Error creando cola de eventos de seguridad");
        vSemaphoreDelete(g_seguridad->mutex_seguridad);
        free(g_seguridad);
        g_seguridad = NULL;
        return ESP_ERR_NO_MEM;
    }
    
    g_seguridad->eventos_sistema = xEventGroupCreate();
    if (!g_seguridad->eventos_sistema) {
        ESP_LOGE(TAG, "Error creando event group de sistema");
        vQueueDelete(g_seguridad->cola_eventos_seguridad);
        vSemaphoreDelete(g_seguridad->mutex_seguridad);
        free(g_seguridad);
        g_seguridad = NULL;
        return ESP_ERR_NO_MEM;
    }
    
    // Crear timers
    esp_timer_create_args_t timer_args;
    
    timer_args.callback = timeout_sesion_callback;
    timer_args.name = "sesion_timeout";
    timer_args.arg = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &g_seguridad->timer_sesion));
    
    timer_args.callback = heartbeat_seguridad_callback;
    timer_args.name = "heartbeat_seguridad";
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &g_seguridad->timer_heartbeat));
    
    timer_args.callback = anti_jamming_callback;
    timer_args.name = "anti_jamming";
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &g_seguridad->timer_anti_jamming));
    
    // Inicializar estadísticas
    g_seguridad->estadisticas.tiempo_ultima_autenticacion = esp_timer_get_time();
    
    g_seguridad->inicializado = true;
    g_seguridad->timestamp_inicializacion = esp_timer_get_time();
    
    ESP_LOGI(TAG, "Módulo de seguridad inicializado correctamente");
    return ESP_OK;
}

esp_err_t vehiculo_seguridad_deinit(void)
{
    if (!g_seguridad) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Desinicializando módulo de seguridad");
    
    // Detener monitoreo
    vehiculo_seguridad_detener_monitoreo();
    
    // Detener timers
    if (g_seguridad->timer_sesion) {
        esp_timer_stop(g_seguridad->timer_sesion);
        esp_timer_delete(g_seguridad->timer_sesion);
    }
    
    if (g_seguridad->timer_heartbeat) {
        esp_timer_stop(g_seguridad->timer_heartbeat);
        esp_timer_delete(g_seguridad->timer_heartbeat);
    }
    
    if (g_seguridad->timer_anti_jamming) {
        esp_timer_stop(g_seguridad->timer_anti_jamming);
        esp_timer_delete(g_seguridad->timer_anti_jamming);
    }
    
    // Liberar recursos
    if (g_seguridad->mutex_seguridad) {
        vSemaphoreDelete(g_seguridad->mutex_seguridad);
    }
    
    if (g_seguridad->cola_eventos_seguridad) {
        vQueueDelete(g_seguridad->cola_eventos_seguridad);
    }
    
    if (g_seguridad->eventos_sistema) {
        vEventGroupDelete(g_seguridad->eventos_sistema);
    }
    
    free(g_seguridad);
    g_seguridad = NULL;
    
    ESP_LOGI(TAG, "Módulo de seguridad desinicializado");
    return ESP_OK;
}

esp_err_t vehiculo_seguridad_iniciar_monitoreo(void)
{
    if (!g_seguridad || !g_seguridad->inicializado) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (g_seguridad->monitoreo_activo) {
        ESP_LOGW(TAG, "Monitoreo de seguridad ya está activo");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Iniciando monitoreo de seguridad");
    
    g_seguridad->monitoreo_activo = true;
    
    // Crear tarea de monitoreo
    BaseType_t ret = xTaskCreate(task_monitoreo_seguridad,
                                "seguridad_monitor",
                                6144,  // Stack size aumentado para seguridad
                                NULL,
                                configMAX_PRIORITIES - 1,  // Máxima prioridad
                                &g_seguridad->task_handle);
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Error creando tarea de monitoreo de seguridad");
        g_seguridad->monitoreo_activo = false;
        return ESP_FAIL;
    }
    
    // Iniciar timers
    esp_timer_start_periodic(g_seguridad->timer_heartbeat, 
                           SEGURIDAD_INTERVALO_HEARTBEAT_MS * 1000);
    
    if (g_seguridad->configuracion.anti_jamming_habilitado) {
        esp_timer_start_periodic(g_seguridad->timer_anti_jamming,
                               g_seguridad->configuracion.tiempo_deteccion_jamming_ms * 1000);
    }
    
    ESP_LOGI(TAG, "Monitoreo de seguridad iniciado");
    return ESP_OK;
}

esp_err_t vehiculo_seguridad_detener_monitoreo(void)
{
    if (!g_seguridad) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!g_seguridad->monitoreo_activo) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Deteniendo monitoreo de seguridad");
    
    g_seguridad->monitoreo_activo = false;
    
    // Esperar a que termine la tarea
    if (g_seguridad->task_handle) {
        for (int i = 0; i < 50; i++) {  // 5 segundos timeout
            if (eTaskGetState(g_seguridad->task_handle) == eDeleted) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        
        if (eTaskGetState(g_seguridad->task_handle) != eDeleted) {
            ESP_LOGW(TAG, "Forzando eliminación de tarea de seguridad");
            vTaskDelete(g_seguridad->task_handle);
        }
        
        g_seguridad->task_handle = NULL;
    }
    
    ESP_LOGI(TAG, "Monitoreo de seguridad detenido");
    return ESP_OK;
}

esp_err_t vehiculo_seguridad_procesar_autenticacion(const uint8_t* datos_autenticacion,
                                                   size_t tamaño_datos,
                                                   int8_t rssi)
{
    if (!g_seguridad || !g_seguridad->inicializado) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!datos_autenticacion || tamaño_datos == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Procesando autenticación (RSSI: %d dBm)", rssi);
    
    uint64_t tiempo_inicio = esp_timer_get_time();
    esp_err_t resultado = ESP_FAIL;
    
    if (xSemaphoreTake(g_seguridad->mutex_seguridad, pdMS_TO_TICKS(1000)) == pdTRUE) {
        
        g_seguridad->estadisticas.intentos_total++;
        
        // Verificar si el sistema está bloqueado
        if (g_seguridad->estado_actual == SEGURIDAD_ESTADO_BLOQUEADO) {
            ESP_LOGW(TAG, "Sistema bloqueado - rechazando autenticación");
            g_seguridad->estadisticas.intentos_bloqueados++;
            xSemaphoreGive(g_seguridad->mutex_seguridad);
            return ESP_ERR_INVALID_STATE;
        }
        
        // Validar proximidad si está habilitada
        if (g_seguridad->configuracion.config_proximidad.metodo != PROXIMIDAD_RSSI ||
            !vehiculo_seguridad_validar_proximidad(rssi, 0, 0)) {
            ESP_LOGW(TAG, "Validación de proximidad fallida");
            
            vehiculo_seguridad_reportar_amenaza(AMENAZA_TIPO_RSSI_ANOMALO,
                                              AMENAZA_NIVEL_BAJA,
                                              (uint32_t)abs(rssi));
        }
        
        // TODO: Implementar validación criptográfica real aquí
        // Por ahora, simulamos autenticación basada en RSSI y tamaño de datos
        bool autenticacion_valida = (rssi > g_seguridad->configuracion.config_proximidad.rssi_minimo &&
                                   rssi < g_seguridad->configuracion.config_proximidad.rssi_maximo &&
                                   tamaño_datos >= 32);  // Mínimo 32 bytes para datos válidos
        
        if (autenticacion_valida) {
            ESP_LOGI(TAG, "Autenticación exitosa");
            
            g_seguridad->estadisticas.intentos_exitosos++;
            g_seguridad->estadisticas.tiempo_ultima_autenticacion = tiempo_inicio;
            g_seguridad->estado_actual = SEGURIDAD_ESTADO_AUTENTICADO;
            
            // Iniciar sesión segura
            g_seguridad->sesion_actual.sesion_activa = true;
            g_seguridad->sesion_actual.timestamp_inicio = tiempo_inicio;
            g_seguridad->sesion_actual.timestamp_ultimo_heartbeat = tiempo_inicio;
            g_seguridad->sesion_actual.numero_sesion = esp_random();
            g_seguridad->sesion_actual.rssi_promedio = rssi;
            g_seguridad->sesion_actual.comandos_procesados = 0;
            g_seguridad->sesion_actual.nivel_amenaza_actual = g_seguridad->nivel_amenaza_global;
            
            // Generar clave de sesión aleatoria
            esp_fill_random(g_seguridad->sesion_actual.clave_sesion, 
                           sizeof(g_seguridad->sesion_actual.clave_sesion));
            
            // Iniciar timer de timeout de sesión
            esp_timer_start_once(g_seguridad->timer_sesion,
                               g_seguridad->configuracion.tiempo_sesion_max_ms * 1000);
            
            g_seguridad->estado_actual = SEGURIDAD_ESTADO_SESION_ACTIVA;
            resultado = ESP_OK;
            
        } else {
            ESP_LOGW(TAG, "Autenticación fallida");
            
            g_seguridad->estadisticas.intentos_fallidos++;
            
            // Verificar si se exceden los intentos máximos
            if (g_seguridad->estadisticas.intentos_fallidos >= 
                g_seguridad->configuracion.max_intentos_autenticacion) {
                
                ESP_LOGE(TAG, "Máximo de intentos de autenticación excedido - bloqueando sistema");
                g_seguridad->estado_actual = SEGURIDAD_ESTADO_BLOQUEADO;
                
                vehiculo_seguridad_reportar_amenaza(AMENAZA_TIPO_BRUTE_FORCE,
                                                  AMENAZA_NIVEL_CRITICA,
                                                  g_seguridad->estadisticas.intentos_fallidos);
            }
            
            resultado = ESP_ERR_NOT_FOUND;  // Autenticación fallida
        }
        
        // Calcular tiempo promedio de autenticación
        uint64_t tiempo_autenticacion = esp_timer_get_time() - tiempo_inicio;
        if (g_seguridad->estadisticas.tiempo_promedio_autenticacion == 0) {
            g_seguridad->estadisticas.tiempo_promedio_autenticacion = tiempo_autenticacion;
        } else {
            // Media móvil simple
            g_seguridad->estadisticas.tiempo_promedio_autenticacion = 
                (g_seguridad->estadisticas.tiempo_promedio_autenticacion * 7 + tiempo_autenticacion) / 8;
        }
        
        xSemaphoreGive(g_seguridad->mutex_seguridad);
    }
    
    return resultado;
}

bool vehiculo_seguridad_validar_proximidad(int8_t rssi, 
                                          uint32_t tiempo_respuesta_us,
                                          uint8_t canal_comunicacion)
{
    if (!g_seguridad || !g_seguridad->inicializado) {
        return false;
    }
    
    const proximidad_config_t* config = &g_seguridad->configuracion.config_proximidad;
    
    // Validación por RSSI
    if (rssi < config->rssi_minimo || rssi > config->rssi_maximo) {
        ESP_LOGW(TAG, "RSSI fuera de rango: %d dBm (min: %d, max: %d)", 
                 rssi, config->rssi_minimo, config->rssi_maximo);
        return false;
    }
    
    // Validación por tiempo de vuelo (si se proporciona)
    if (tiempo_respuesta_us > 0) {
        if (tiempo_respuesta_us > config->tiempo_vuelo_max_us) {
            ESP_LOGW(TAG, "Tiempo de respuesta sospechoso: %d us (max: %d us)",
                     tiempo_respuesta_us, config->tiempo_vuelo_max_us);
            
            vehiculo_seguridad_reportar_amenaza(AMENAZA_TIPO_TIEMPO_RESPUESTA,
                                              AMENAZA_NIVEL_MEDIA,
                                              tiempo_respuesta_us);
            return false;
        }
    }
    
    // Validación por coherencia RSSI-distancia
    // RSSI muy alto puede indicar ataque relay con amplificadores
    if (rssi > -30) {  // Demasiado cerca para uso normal vehicular
        ESP_LOGW(TAG, "RSSI anómalamente alto: %d dBm - posible ataque relay", rssi);
        
        vehiculo_seguridad_reportar_amenaza(AMENAZA_TIPO_RELAY_ATTACK,
                                          AMENAZA_NIVEL_ALTA,
                                          (uint32_t)abs(rssi));
        return false;
    }
    
    ESP_LOGI(TAG, "Validación de proximidad exitosa (RSSI: %d dBm)", rssi);
    return true;
}

bool vehiculo_seguridad_detectar_relay_attack(uint32_t tiempo_respuesta_us,
                                             int8_t rssi,
                                             uint8_t numero_saltos_frecuencia)
{
    if (!g_seguridad || !g_seguridad->inicializado) {
        return false;
    }
    
    bool relay_detectado = false;
    
    // Criterio 1: Tiempo de respuesta anómalo
    if (tiempo_respuesta_us > 10000) {  // 10ms es sospechoso para LoRa local
        ESP_LOGW(TAG, "Tiempo de respuesta anómalo: %d us", tiempo_respuesta_us);
        relay_detectado = true;
    }
    
    // Criterio 2: RSSI inconsistente con distancia esperada
    if (rssi > -20) {  // Demasiado fuerte
        ESP_LOGW(TAG, "RSSI anómalamente alto: %d dBm", rssi);
        relay_detectado = true;
    }
    
    // Criterio 3: Patrón de saltos de frecuencia anómalo
    if (numero_saltos_frecuencia > SEGURIDAD_INTENTOS_FREQUENCY_HOP) {
        ESP_LOGW(TAG, "Demasiados saltos de frecuencia: %d", numero_saltos_frecuencia);
        relay_detectado = true;
    }
    
    // Criterio 4: Comparar con tiempo promedio histórico
    if (g_seguridad->estadisticas.tiempo_promedio_autenticacion > 0) {
        uint64_t tiempo_promedio_ms = g_seguridad->estadisticas.tiempo_promedio_autenticacion / 1000;
        
        if (tiempo_respuesta_us > (tiempo_promedio_ms * 3000)) {  // 3x más lento
            ESP_LOGW(TAG, "Tiempo muy superior al promedio histórico");
            relay_detectado = true;
        }
    }
    
    if (relay_detectado) {
        ESP_LOGE(TAG, "ATAQUE RELAY DETECTADO");
        
        vehiculo_seguridad_reportar_amenaza(AMENAZA_TIPO_RELAY_ATTACK,
                                          AMENAZA_NIVEL_CRITICA,
                                          tiempo_respuesta_us);
        
        g_seguridad->estadisticas.detecciones_relay++;
    }
    
    return relay_detectado;
}

bool vehiculo_seguridad_detectar_jamming(void)
{
    if (!g_seguridad || !g_seguridad->configuracion.anti_jamming_habilitado) {
        return false;
    }
    
    uint64_t tiempo_actual = esp_timer_get_time();
    uint64_t tiempo_sin_comunicacion = tiempo_actual - 
                                      g_seguridad->estadisticas.tiempo_ultima_autenticacion;
    
    // Si hace mucho tiempo que no hay comunicación, puede ser jamming
    if (tiempo_sin_comunicacion > (g_seguridad->configuracion.tiempo_deteccion_jamming_ms * 1000)) {
        ESP_LOGW(TAG, "Posible jamming: %llu ms sin comunicación", 
                 tiempo_sin_comunicacion / 1000);
        
        g_seguridad->estadisticas.detecciones_jamming++;
        return true;
    }
    
    return false;
}

esp_err_t vehiculo_seguridad_procesar_heartbeat(const uint8_t* datos_heartbeat,
                                               size_t tamaño_datos,
                                               int8_t rssi)
{
    if (!g_seguridad || !g_seguridad->inicializado) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!datos_heartbeat || tamaño_datos == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!g_seguridad->sesion_actual.sesion_activa) {
        ESP_LOGW(TAG, "Heartbeat recibido sin sesión activa");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Heartbeat recibido (RSSI: %d dBm)", rssi);
    
    if (xSemaphoreTake(g_seguridad->mutex_seguridad, pdMS_TO_TICKS(500)) == pdTRUE) {
        
        g_seguridad->sesion_actual.timestamp_ultimo_heartbeat = esp_timer_get_time();
        
        // Actualizar RSSI promedio de la sesión
        g_seguridad->sesion_actual.rssi_promedio = 
            (g_seguridad->sesion_actual.rssi_promedio + rssi) / 2;
        
        // Verificar coherencia del RSSI
        int8_t diferencia_rssi = abs(rssi - g_seguridad->sesion_actual.rssi_promedio);
        if (diferencia_rssi > 20) {  // Variación > 20dBm es sospechosa
            ESP_LOGW(TAG, "Variación de RSSI sospechosa: %d dBm", diferencia_rssi);
            
            vehiculo_seguridad_reportar_amenaza(AMENAZA_TIPO_RSSI_ANOMALO,
                                              AMENAZA_NIVEL_BAJA,
                                              diferencia_rssi);
        }
        
        xSemaphoreGive(g_seguridad->mutex_seguridad);
    }
    
    return ESP_OK;
}

esp_err_t vehiculo_seguridad_reportar_amenaza(amenaza_tipo_t tipo_amenaza,
                                             amenaza_nivel_t nivel_amenaza,
                                             uint32_t datos_adicionales)
{
    if (!g_seguridad || !g_seguridad->inicializado) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGW(TAG, "Reportando amenaza tipo 0x%02X nivel %d", tipo_amenaza, nivel_amenaza);
    
    evento_seguridad_t evento = {
        .tipo_amenaza = tipo_amenaza,
        .nivel_amenaza = nivel_amenaza,
        .timestamp = esp_timer_get_time(),
        .rssi_asociado = 0,
        .datos_adicionales = datos_adicionales
    };
    
    // Generar descripción de la amenaza
    strncpy(evento.descripcion, vehiculo_seguridad_tipo_amenaza_a_string(tipo_amenaza), 
           sizeof(evento.descripcion) - 1);
    
    // Enviar evento a la cola
    if (xQueueSend(g_seguridad->cola_eventos_seguridad, &evento, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Cola de eventos de seguridad llena - evento perdido");
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}

esp_err_t vehiculo_seguridad_cerrar_sesion(const char* motivo)
{
    if (!g_seguridad || !g_seguridad->inicializado) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Cerrando sesión de seguridad: %s", motivo ? motivo : "Sin motivo");
    
    if (xSemaphoreTake(g_seguridad->mutex_seguridad, pdMS_TO_TICKS(1000)) == pdTRUE) {
        
        // Limpiar datos de sesión
        memset(&g_seguridad->sesion_actual, 0, sizeof(sesion_seguridad_t));
        
        // Detener timer de sesión
        esp_timer_stop(g_seguridad->timer_sesion);
        
        // Cambiar estado
        g_seguridad->estado_actual = SEGURIDAD_ESTADO_NO_AUTENTICADO;
        
        xSemaphoreGive(g_seguridad->mutex_seguridad);
    }
    
    return ESP_OK;
}

esp_err_t vehiculo_seguridad_obtener_estado(seguridad_estado_t* estado)
{
    if (!g_seguridad || !g_seguridad->inicializado || !estado) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *estado = g_seguridad->estado_actual;
    return ESP_OK;
}

esp_err_t vehiculo_seguridad_obtener_evento(evento_seguridad_t* evento, uint32_t timeout_ms)
{
    if (!g_seguridad || !g_seguridad->inicializado || !evento) {
        return ESP_ERR_INVALID_ARG;
    }
    
    TickType_t timeout_ticks = (timeout_ms == 0) ? 0 : 
                              (timeout_ms == UINT32_MAX) ? portMAX_DELAY :
                              pdMS_TO_TICKS(timeout_ms);
    
    if (xQueueReceive(g_seguridad->cola_eventos_seguridad, evento, timeout_ticks) == pdTRUE) {
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

// ============================================================================
// FUNCIONES DE UTILIDAD
// ============================================================================

const char* vehiculo_seguridad_estado_a_string(seguridad_estado_t estado)
{
    switch (estado) {
        case SEGURIDAD_ESTADO_DESCONOCIDO: return "DESCONOCIDO";
        case SEGURIDAD_ESTADO_NO_AUTENTICADO: return "NO_AUTENTICADO";
        case SEGURIDAD_ESTADO_AUTENTICANDO: return "AUTENTICANDO";
        case SEGURIDAD_ESTADO_AUTENTICADO: return "AUTENTICADO";
        case SEGURIDAD_ESTADO_SESION_ACTIVA: return "SESION_ACTIVA";
        case SEGURIDAD_ESTADO_BLOQUEADO: return "BLOQUEADO";
        case SEGURIDAD_ESTADO_EMERGENCIA: return "EMERGENCIA";
        case SEGURIDAD_ESTADO_TAMPER: return "TAMPER";
        case SEGURIDAD_ESTADO_JAMMING_DETECTADO: return "JAMMING_DETECTADO";
        default: return "INDEFINIDO";
    }
}

const char* vehiculo_seguridad_nivel_amenaza_a_string(amenaza_nivel_t nivel)
{
    switch (nivel) {
        case AMENAZA_NIVEL_NINGUNA: return "NINGUNA";
        case AMENAZA_NIVEL_BAJA: return "BAJA";
        case AMENAZA_NIVEL_MEDIA: return "MEDIA";
        case AMENAZA_NIVEL_ALTA: return "ALTA";
        case AMENAZA_NIVEL_CRITICA: return "CRITICA";
        default: return "DESCONOCIDO";
    }
}

const char* vehiculo_seguridad_tipo_amenaza_a_string(amenaza_tipo_t tipo)
{
    switch (tipo) {
        case AMENAZA_TIPO_INTRUSION_FISICA: return "INTRUSION_FISICA";
        case AMENAZA_TIPO_TAMPER: return "TAMPER";
        case AMENAZA_TIPO_RELAY_ATTACK: return "RELAY_ATTACK";
        case AMENAZA_TIPO_JAMMING: return "JAMMING";
        case AMENAZA_TIPO_REPLAY_ATTACK: return "REPLAY_ATTACK";
        case AMENAZA_TIPO_BRUTE_FORCE: return "BRUTE_FORCE";
        case AMENAZA_TIPO_TIEMPO_RESPUESTA: return "TIEMPO_RESPUESTA";
        case AMENAZA_TIPO_RSSI_ANOMALO: return "RSSI_ANOMALO";
        case AMENAZA_TIPO_FRECUENCIA_ANOMALA: return "FRECUENCIA_ANOMALA";
        case AMENAZA_TIPO_BATERIA_CRITICA: return "BATERIA_CRITICA";
        default: return "DESCONOCIDO";
    }
}
