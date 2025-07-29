/**
 * @file app_vehiculo.h 
 * @brief Aplicación principal del módulo vehicular
 * 
 * Sistema de alarma vehicular ESP32-S3 con comunicación LoRa AU915
 * Manejo de actuadores, sensores y protocolos de seguridad
 * 
 * @author Sistema Alarma Vehicular
 * @date 2024
 */

#ifndef APP_VEHICULO_H
#define APP_VEHICULO_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "nvs_flash.h"

// Configuración de hardware
#include "gpio_config.h"

// Componentes del sistema vehicular
#include "vehiculo_actuadores.h"  // Tipos: actuador_config_t
#include "vehiculo_sensores.h"    // Tipos: sensor_estado_t, sensor_config_t

// Componentes compartidos
#include "protocolo_seguro.h"
#include "cripto_utils.h"
#include "config_au915.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// CONFIGURACIÓN DEL SISTEMA
// ============================================================================

#define VEHICULO_TAG "VEHICULO"
#define VEHICULO_VERSION "1.0.0"

// Prioridades de tareas FreeRTOS
#define PRIO_SEGURIDAD_TASK     (configMAX_PRIORITIES - 1)  // Máxima prioridad
#define PRIO_COMUNICACION_TASK  (configMAX_PRIORITIES - 2)  // Alta prioridad
#define PRIO_SENSORES_TASK      (tskIDLE_PRIORITY + 3)       // Media prioridad
#define PRIO_ACTUADORES_TASK    (tskIDLE_PRIORITY + 2)       // Media prioridad
#define PRIO_MONITOR_TASK       (tskIDLE_PRIORITY + 1)       // Baja prioridad

// Stack sizes para tareas críticas
#define STACK_SEGURIDAD    (8192)   // Operaciones criptográficas
#define STACK_COMUNICACION (6144)   // LoRa + BLE
#define STACK_SENSORES     (4096)   // Lectura de sensores
#define STACK_ACTUADORES   (4096)   // Control de relés
#define STACK_MONITOR      (3072)   // Sistema y telemetría

// Timeouts críticos
#define TIMEOUT_AUTENTICACION_MS     (5000)
#define TIMEOUT_RESPUESTA_LLAVE_MS   (3000)
#define TIMEOUT_EMERGENCIA_MS        (10000)
#define INTERVALO_HEARTBEAT_MS       (30000)
#define INTERVALO_SENSORES_MS        (100)

// ============================================================================
// ESTRUCTURAS DE DATOS
// ============================================================================

/**
 * @brief Estados del sistema vehicular
 */
typedef enum {
    VEHICULO_ESTADO_DESARMADO = 0,      // Sistema desactivado
    VEHICULO_ESTADO_ARMADO,             // Sistema activo, monitoreando
    VEHICULO_ESTADO_ALERTA,             // Detección de amenaza
    VEHICULO_ESTADO_ALARMA_ACTIVA,      // Alarma sonando
    VEHICULO_ESTADO_PANICO,             // Modo pánico activado
    VEHICULO_ESTADO_MANTENIMIENTO,      // Modo servicio técnico
    VEHICULO_ESTADO_ERROR_CRITICO       // Fallo del sistema
} vehiculo_estado_t;

/**
 * @brief Comandos del sistema
 */
typedef enum {
    CMD_ARMAR_SISTEMA = 0x01,
    CMD_DESARMAR_SISTEMA = 0x02,
    CMD_PANICO_VEHICULO = 0x03,      // Renombrado para evitar conflicto
    CMD_LOCALIZAR = 0x04,
    CMD_STATUS = 0x05,
    CMD_CONFIGURAR = 0x06,
    CMD_RESET_ALARMA = 0x07,
    CMD_MODO_MANTENIMIENTO = 0x08,
    CMD_HEARTBEAT = 0x09,
    CMD_OTA_UPDATE = 0x0A
} vehiculo_comando_t;

/**
 * @brief Eventos del sistema
 */
typedef enum {
    EVT_INTRUSION_DETECTADA = BIT0,
    EVT_TAMPER_DETECTADO = BIT1,
    EVT_BATERIA_BAJA = BIT2,
    EVT_COMUNICACION_PERDIDA = BIT3,
    EVT_COMANDO_RECIBIDO = BIT4,
    EVT_EMERGENCIA = BIT5,
    EVT_HEARTBEAT = BIT6,
    EVT_SISTEMA_CRITICO = BIT7
} vehiculo_eventos_t;

/**
 * @brief Estructura principal de la aplicación
 */
typedef struct {
    // Estado del sistema
    vehiculo_estado_t estado_actual;
    vehiculo_estado_t estado_anterior;
    
    // Configuración hardware
    actuador_config_simplificado_t config_actuadores;
    sensor_config_t config_sensores;
    
    // Estado runtime
    sensor_estado_simplificado_t estado_sensores;
    bool sistema_armado;
    bool alarma_activa;
    bool modo_panico;
    
    // Comunicación
    QueueHandle_t cola_comandos;
    QueueHandle_t cola_eventos;
    EventGroupHandle_t eventos_sistema;
    SemaphoreHandle_t mutex_estado;
    
    // Timers
    esp_timer_handle_t timer_heartbeat;
    esp_timer_handle_t timer_autenticacion;
    esp_timer_handle_t timer_emergencia;
    
    // Contadores de seguridad
    uint32_t intentos_autenticacion_fallidos;
    uint32_t detecciones_intrusion;
    uint64_t tiempo_ultima_comunicacion;
    
    // Configuración de seguridad
    uint8_t clave_maestra[32];
    uint8_t id_vehiculo[16];
    uint32_t codigo_rolling_actual;
    
    // Estado de actuadores
    bool estado_reles[4];
    bool buzzer_activo;
    uint64_t tiempo_activacion_actuadores;
    
} vehiculo_app_t;

// ============================================================================
// PROTOTIPOS DE FUNCIONES PRINCIPALES
// ============================================================================

/**
 * @brief Inicializa la aplicación vehicular
 * @return ESP_OK si éxito, error code si falla
 */
esp_err_t vehiculo_app_init(void);

/**
 * @brief Ejecuta el bucle principal de la aplicación
 * @return ESP_OK si éxito, no debería retornar en operación normal
 */
esp_err_t vehiculo_app_run(void);

/**
 * @brief Maneja comandos recibidos desde la llave
 * @param comando Comando recibido
 * @param datos Datos adicionales del comando
 * @param tamaño Tamaño de los datos
 * @return ESP_OK si comando procesado correctamente
 */
esp_err_t vehiculo_procesar_comando(vehiculo_comando_t comando, 
                                  const uint8_t* datos, 
                                  size_t tamaño);

/**
 * @brief Cambia el estado del sistema
 * @param nuevo_estado Estado objetivo
 * @return ESP_OK si transición válida
 */
esp_err_t vehiculo_cambiar_estado(vehiculo_estado_t nuevo_estado);

/**
 * @brief Obtiene el estado actual del sistema
 * @return Estado actual
 */
vehiculo_estado_t vehiculo_obtener_estado(void);

/**
 * @brief Activa/desactiva el sistema de alarma
 * @param armar true para armar, false para desarmar
 * @return ESP_OK si éxito
 */
esp_err_t vehiculo_armar_sistema(bool armar);

/**
 * @brief Activa modo pánico
 * @return ESP_OK si éxito
 */
esp_err_t vehiculo_activar_panico(void);

/**
 * @brief Manejo de emergencias del sistema
 * @param tipo_emergencia Tipo de emergencia detectada
 * @return ESP_OK si manejada correctamente
 */
esp_err_t vehiculo_manejar_emergencia(vehiculo_eventos_t tipo_emergencia);

// ============================================================================
// PROTOTIPOS DE TAREAS FREERTOS
// ============================================================================

/**
 * @brief Tarea de seguridad y criptografía (CPU1, máxima prioridad)
 * @param pvParameters Parámetros de inicialización
 */
void vehiculo_task_seguridad(void* pvParameters);

/**
 * @brief Tarea de comunicación LoRa/BLE (CPU0, alta prioridad)
 * @param pvParameters Parámetros de inicialización
 */
void vehiculo_task_comunicacion(void* pvParameters);

/**
 * @brief Tarea de monitoreo de sensores (CPU0, media prioridad)
 * @param pvParameters Parámetros de inicialización
 */
void vehiculo_task_sensores(void* pvParameters);

/**
 * @brief Tarea de control de actuadores (CPU1, media prioridad)
 * @param pvParameters Parámetros de inicialización
 */
void vehiculo_task_actuadores(void* pvParameters);

/**
 * @brief Tarea de monitoreo del sistema (CPU0, baja prioridad)
 * @param pvParameters Parámetros de inicialización
 */
void vehiculo_task_monitor_sistema(void* pvParameters);

// ============================================================================
// FUNCIONES DE UTILIDAD
// ============================================================================

/**
 * @brief Obtiene instancia global de la aplicación
 * @return Puntero a la estructura principal
 */
vehiculo_app_t* vehiculo_get_app_instance(void);

/**
 * @brief Reinicia el sistema de forma segura
 * @param motivo Motivo del reinicio para logging
 */
void vehiculo_reinicio_seguro(const char* motivo);

/**
 * @brief Verifica integridad del sistema
 * @return true si sistema íntegro
 */
bool vehiculo_verificar_integridad(void);

/**
 * @brief Genera reporte de estado para diagnóstico
 * @param buffer Buffer de salida
 * @param tamaño Tamaño del buffer
 * @return Bytes escritos en el buffer
 */
size_t vehiculo_generar_reporte_estado(char* buffer, size_t tamaño);

#ifdef __cplusplus
}
#endif

#endif // APP_VEHICULO_H
