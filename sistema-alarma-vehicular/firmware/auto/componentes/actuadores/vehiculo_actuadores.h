/**
 * @file vehiculo_actuadores.h
 * @brief Control de actuadores del vehículo (relés, buzzer, LEDs)
 * 
 * Manejo seguro de actuadores con protección contra activación prolongada
 * y verificación de integridad del hardware
 * 
 * @author Sistema Alarma Vehicular
 * @date 2024
 */

#ifndef VEHICULO_ACTUADORES_H
#define VEHICULO_ACTUADORES_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// DEFINICIONES Y CONSTANTES
// ============================================================================

#define ACTUADORES_TAG "VEH_ACTUADORES"

// Tiempos de seguridad (en microsegundos)
#define TIEMPO_MAX_SIRENA_US        (5 * 60 * 1000000ULL)   // 5 minutos máximo
#define TIEMPO_MAX_LUCES_US         (10 * 60 * 1000000ULL)  // 10 minutos máximo
#define TIEMPO_MAX_BLOQUEO_US       (30 * 60 * 1000000ULL)  // 30 minutos máximo
#define TIEMPO_MAX_TRACCION_US      (60 * 60 * 1000000ULL)  // 1 hora máximo
#define TIEMPO_MAX_BUZZER_US        (30 * 1000000ULL)       // 30 segundos máximo

// Intervalos de verificación
#define INTERVALO_VERIFICACION_MS   500
// Configuración temporal - usar definiciones del main
// #define INTERVALO_HEARTBEAT_MS      1000

// Estados de actuadores
typedef enum {
    ACTUADOR_ESTADO_INACTIVO = 0,
    ACTUADOR_ESTADO_ACTIVO,
    ACTUADOR_ESTADO_ERROR,
    ACTUADOR_ESTADO_TIMEOUT
} actuador_estado_t;

// Tipos de actuadores
typedef enum {
    ACTUADOR_SIRENA = 0,        // Relé 1 - Sirena/claxon principal
    ACTUADOR_LUCES,             // Relé 2 - Luces intermitentes
    ACTUADOR_BLOQUEO_MOTOR,     // Relé 3 - Bloqueo del motor
    ACTUADOR_TRACCION,          // Relé 4 - Sistema de rastreo/GPS
    ACTUADOR_BUZZER,            // Buzzer local de advertencia
    ACTUADOR_LED_ESTADO,        // LED indicador de estado
    ACTUADOR_MAX_COUNT
} actuador_tipo_t;

// Configuración de un actuador individual
typedef struct {
    gpio_num_t pin;                     // Pin GPIO del actuador
    bool activo_alto;                   // true = activo en alto, false = activo en bajo
    uint64_t tiempo_max_activacion_us;  // Tiempo máximo de activación continua
    uint32_t corriente_max_ma;          // Corriente máxima esperada (para monitoreo)
    const char* nombre;                 // Nombre descriptivo del actuador
    bool critico_seguridad;             // true si requiere manejo especial de seguridad
} actuador_config_t;

// Estado runtime de un actuador
typedef struct {
    actuador_estado_t estado;           // Estado actual
    bool activo;                        // Si está actualmente activado
    uint64_t tiempo_activacion_inicio;  // Timestamp de inicio de activación
    uint64_t tiempo_total_activo;       // Tiempo total activo desde reset
    uint32_t conteo_activaciones;       // Número de veces activado
    uint32_t conteo_errores;            // Número de errores detectados
    esp_timer_handle_t timer_timeout;   // Timer de timeout de seguridad
} actuador_runtime_t;

// Estructura principal del módulo de actuadores
typedef struct {
    actuador_config_t config[ACTUADOR_MAX_COUNT];       // Configuración de cada actuador
    actuador_runtime_t runtime[ACTUADOR_MAX_COUNT];     // Estado runtime de cada actuador
    SemaphoreHandle_t mutex_actuadores;                 // Mutex para acceso thread-safe
    esp_timer_handle_t timer_verificacion;              // Timer de verificación periódica
    bool inicializado;                                  // Flag de inicialización
    uint32_t conteo_operaciones_totales;                // Contador de operaciones
    uint64_t timestamp_ultimo_heartbeat;                // Último heartbeat del sistema
} vehiculo_actuadores_t;

// ============================================================================
// PROTOTIPOS DE FUNCIONES PÚBLICAS
// ============================================================================

/**
 * @brief Inicializa el módulo de actuadores del vehículo
 * @return ESP_OK si éxito, código de error si falla
 */
esp_err_t vehiculo_actuadores_init(void);

/**
 * @brief Desinicializa el módulo de actuadores
 * @return ESP_OK si éxito
 */
esp_err_t vehiculo_actuadores_deinit(void);

/**
 * @brief Activa un actuador específico
 * @param tipo Tipo de actuador a activar
 * @param duracion_ms Duración de activación en ms (0 = indefinido hasta apagar)
 * @return ESP_OK si éxito, código de error si falla
 */
esp_err_t vehiculo_actuador_activar(actuador_tipo_t tipo, uint32_t duracion_ms);

/**
 * @brief Desactiva un actuador específico
 * @param tipo Tipo de actuador a desactivar
 * @return ESP_OK si éxito, código de error si falla
 */
esp_err_t vehiculo_actuador_desactivar(actuador_tipo_t tipo);

/**
 * @brief Desactiva todos los actuadores inmediatamente
 * @return ESP_OK si éxito
 */
esp_err_t vehiculo_actuadores_apagar_todos(void);

/**
 * @brief Obtiene el estado actual de un actuador
 * @param tipo Tipo de actuador
 * @param estado Puntero para retornar el estado
 * @return ESP_OK si éxito, ESP_ERR_INVALID_ARG si parámetros inválidos
 */
esp_err_t vehiculo_actuador_obtener_estado(actuador_tipo_t tipo, actuador_estado_t* estado);

/**
 * @brief Verifica si un actuador está actualmente activo
 * @param tipo Tipo de actuador
 * @param activo Puntero para retornar el estado de activación
 * @return ESP_OK si éxito, ESP_ERR_INVALID_ARG si parámetros inválidos
 */
esp_err_t vehiculo_actuador_esta_activo(actuador_tipo_t tipo, bool* activo);

/**
 * @brief Activa secuencia de alarma (sirena + luces intermitentes)
 * @param duracion_ms Duración de la secuencia en ms
 * @return ESP_OK si éxito
 */
esp_err_t vehiculo_activar_secuencia_alarma(uint32_t duracion_ms);

/**
 * @brief Activa secuencia de pánico (todos los actuadores de seguridad)
 * @return ESP_OK si éxito
 */
esp_err_t vehiculo_activar_secuencia_panico(void);

/**
 * @brief Activa secuencia de localización (luces + buzzer intermitente)
 * @param duracion_ms Duración de la secuencia en ms
 * @return ESP_OK si éxito
 */
esp_err_t vehiculo_activar_secuencia_localizacion(uint32_t duracion_ms);

/**
 * @brief Configura un actuador específico
 * @param tipo Tipo de actuador
 * @param config Nueva configuración del actuador
 * @return ESP_OK si éxito, código de error si falla
 */
esp_err_t vehiculo_actuador_configurar(actuador_tipo_t tipo, const actuador_config_t* config);

/**
 * @brief Obtiene estadísticas de uso de un actuador
 * @param tipo Tipo de actuador
 * @param tiempo_total_activo Puntero para retornar tiempo total activo (us)
 * @param conteo_activaciones Puntero para retornar número de activaciones
 * @param conteo_errores Puntero para retornar número de errores
 * @return ESP_OK si éxito, ESP_ERR_INVALID_ARG si parámetros inválidos
 */
esp_err_t vehiculo_actuador_obtener_estadisticas(actuador_tipo_t tipo, 
                                                uint64_t* tiempo_total_activo,
                                                uint32_t* conteo_activaciones,
                                                uint32_t* conteo_errores);

/**
 * @brief Verifica la integridad del sistema de actuadores
 * @param reporte Buffer para generar reporte de integridad
 * @param tamaño_buffer Tamaño del buffer de reporte
 * @return true si sistema íntegro, false si hay problemas
 */
bool vehiculo_actuadores_verificar_integridad(char* reporte, size_t tamaño_buffer);

/**
 * @brief Ejecuta autotest de todos los actuadores
 * @param duracion_test_ms Duración del test por actuador en ms
 * @return ESP_OK si todos los tests pasaron, ESP_FAIL si alguno falló
 */
esp_err_t vehiculo_actuadores_autotest(uint32_t duracion_test_ms);

/**
 * @brief Fuerza el apagado inmediato de todos los actuadores (emergencia)
 * Esta función bypasea todos los checks de seguridad
 */
void vehiculo_actuadores_apagado_emergencia(void);

/**
 * @brief Obtiene la configuración actual del módulo
 * @return Puntero a la estructura de actuadores (solo lectura)
 */
const vehiculo_actuadores_t* vehiculo_actuadores_obtener_instancia(void);

// ============================================================================
// FUNCIONES DE UTILIDAD
// ============================================================================

/**
 * @brief Convierte tipo de actuador a string
 * @param tipo Tipo de actuador
 * @return String descriptivo del actuador
 */
const char* vehiculo_actuador_tipo_a_string(actuador_tipo_t tipo);

/**
 * @brief Convierte estado de actuador a string
 * @param estado Estado del actuador
 * @return String descriptivo del estado
 */
const char* vehiculo_actuador_estado_a_string(actuador_estado_t estado);

// ============================================================================
// ESTRUCTURAS DE COMPATIBILIDAD CON MAIN
// ============================================================================

/**
 * @brief Configuración simplificada de actuadores para compatibilidad con main
 */
typedef struct {
    gpio_num_t pin_rele1;       // Sirena/claxon
    gpio_num_t pin_rele2;       // Luces intermitentes
    gpio_num_t pin_rele3;       // Bloqueo motor
    gpio_num_t pin_rele4;       // Tracción/localización
    gpio_num_t pin_buzzer;      // Indicador sonoro local
    gpio_num_t pin_led_estado;  // LED de estado
    uint32_t tiempo_activacion_max_ms;  // Seguridad ante fallos
} actuador_config_simplificado_t;

#ifdef __cplusplus
}
#endif

#endif // VEHICULO_ACTUADORES_H
