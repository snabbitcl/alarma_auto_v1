/**
 * @file vehiculo_seguridad.h
 * @brief Sistema de seguridad avanzado del módulo vehicular
 * 
 * Implementa múltiples capas de seguridad incluyendo validación de proximidad,
 * detección de ataques de relay, anti-jamming y respuesta ante amenazas
 * 
 * @author Sistema Alarma Vehicular
 * @date 2024
 */

#ifndef VEHICULO_SEGURIDAD_H
#define VEHICULO_SEGURIDAD_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// DEFINICIONES Y CONSTANTES
// ============================================================================

#define SEGURIDAD_TAG "VEH_SEGURIDAD"

// Configuración de seguridad
#define SEGURIDAD_MAX_INTENTOS_AUTENTICACION   5       // Máximo intentos antes de bloqueo
#define SEGURIDAD_TIEMPO_BLOQUEO_MS           30000    // 30 segundos de bloqueo
#define SEGURIDAD_TIEMPO_SESION_MS            300000   // 5 minutos de sesión activa
#define SEGURIDAD_INTERVALO_HEARTBEAT_MS      30000    // 30 segundos entre heartbeats
#define SEGURIDAD_TIMEOUT_RESPUESTA_MS        3000     // 3 segundos timeout respuesta
#define SEGURIDAD_RSSI_UMBRAL_CERCANO         -40      // RSSI para proximidad cercana
#define SEGURIDAD_RSSI_UMBRAL_MEDIO           -60      // RSSI para proximidad media
#define SEGURIDAD_RSSI_UMBRAL_LEJANO          -80      // RSSI para proximidad lejana

// Configuración anti-jamming
#define SEGURIDAD_TIEMPO_SIN_COMUNICACION_MS  60000    // 1 minuto sin comunicación = jamming
#define SEGURIDAD_INTENTOS_FREQUENCY_HOP      5        // Intentos de salto de frecuencia
#define SEGURIDAD_CANALES_BACKUP              3        // Canales de respaldo

// Estados de seguridad
typedef enum {
    SEGURIDAD_ESTADO_DESCONOCIDO = 0,       // Estado inicial/desconocido
    SEGURIDAD_ESTADO_NO_AUTENTICADO,        // Sin autenticación válida
    SEGURIDAD_ESTADO_AUTENTICANDO,          // Proceso de autenticación en curso
    SEGURIDAD_ESTADO_AUTENTICADO,           // Autenticación válida
    SEGURIDAD_ESTADO_SESION_ACTIVA,         // Sesión segura establecida
    SEGURIDAD_ESTADO_BLOQUEADO,             // Sistema bloqueado por seguridad
    SEGURIDAD_ESTADO_EMERGENCIA,            // Modo emergencia activado
    SEGURIDAD_ESTADO_TAMPER,                // Tamper detectado
    SEGURIDAD_ESTADO_JAMMING_DETECTADO      // Interferencia detectada
} seguridad_estado_t;

// Niveles de amenaza
typedef enum {
    AMENAZA_NIVEL_NINGUNA = 0,              // Sin amenazas detectadas
    AMENAZA_NIVEL_BAJA,                     // Amenaza menor (monitoreo)
    AMENAZA_NIVEL_MEDIA,                    // Amenaza moderada (alerta)
    AMENAZA_NIVEL_ALTA,                     // Amenaza alta (alarma)
    AMENAZA_NIVEL_CRITICA                   // Amenaza crítica (pánico)
} amenaza_nivel_t;

// Tipos de amenazas
typedef enum {
    AMENAZA_TIPO_INTRUSION_FISICA = BIT0,   // Intrusión física detectada
    AMENAZA_TIPO_TAMPER = BIT1,             // Manipulación del sistema
    AMENAZA_TIPO_RELAY_ATTACK = BIT2,       // Ataque de relay detectado
    AMENAZA_TIPO_JAMMING = BIT3,            // Interferencia de comunicación
    AMENAZA_TIPO_REPLAY_ATTACK = BIT4,      // Ataque de replay detectado
    AMENAZA_TIPO_BRUTE_FORCE = BIT5,        // Ataque de fuerza bruta
    AMENAZA_TIPO_TIEMPO_RESPUESTA = BIT6,   // Tiempo de respuesta anómalo
    AMENAZA_TIPO_RSSI_ANOMALO = BIT7,       // RSSI anómalo para la distancia
    AMENAZA_TIPO_FRECUENCIA_ANOMALA = BIT8, // Patrón de frecuencia anómalo
    AMENAZA_TIPO_BATERIA_CRITICA = BIT9     // Batería en estado crítico
} amenaza_tipo_t;

// Métodos de validación de proximidad
typedef enum {
    PROXIMIDAD_RSSI = 0,                    // Validación por RSSI
    PROXIMIDAD_TIEMPO_VUELO,                // Validación por tiempo de vuelo
    PROXIMIDAD_TRIANGULACION,               // Validación por triangulación
    PROXIMIDAD_MULTICANAL                   // Validación multi-canal
} proximidad_metodo_t;

// Configuración de validación de proximidad
typedef struct {
    proximidad_metodo_t metodo;             // Método de validación principal
    int8_t rssi_minimo;                     // RSSI mínimo aceptable
    int8_t rssi_maximo;                     // RSSI máximo aceptable
    uint32_t tiempo_vuelo_max_us;           // Tiempo de vuelo máximo en microsegundos
    uint32_t tolerancia_timing_us;          // Tolerancia en timing
    bool validacion_dual_radio;             // Usar LoRa + BLE para validar
    uint8_t canales_verificacion[SEGURIDAD_CANALES_BACKUP]; // Canales para verificación
} proximidad_config_t;

// Estadísticas de autenticación
typedef struct {
    uint32_t intentos_total;                // Total de intentos de autenticación
    uint32_t intentos_exitosos;             // Intentos exitosos
    uint32_t intentos_fallidos;             // Intentos fallidos
    uint32_t intentos_bloqueados;           // Intentos bloqueados por seguridad
    uint32_t detecciones_relay;             // Detecciones de ataque relay
    uint32_t detecciones_replay;            // Detecciones de ataque replay
    uint32_t detecciones_jamming;           // Detecciones de jamming
    uint64_t tiempo_ultima_autenticacion;   // Timestamp última autenticación válida
    uint64_t tiempo_promedio_autenticacion; // Tiempo promedio de autenticación
} autenticacion_stats_t;

// Información de sesión de seguridad
typedef struct {
    bool sesion_activa;                     // Si hay sesión activa
    uint64_t timestamp_inicio;              // Inicio de sesión
    uint64_t timestamp_ultimo_heartbeat;    // Último heartbeat recibido
    uint32_t numero_sesion;                 // Número de sesión único
    uint8_t clave_sesion[32];               // Clave de sesión actual
    uint8_t id_llave_autorizada[16];        // ID de la llave autorizada
    int8_t rssi_promedio;                   // RSSI promedio de la sesión
    uint32_t comandos_procesados;           // Comandos procesados en esta sesión
    amenaza_nivel_t nivel_amenaza_actual;   // Nivel de amenaza actual
} sesion_seguridad_t;

// Configuración del sistema de seguridad
typedef struct {
    // Configuración general
    bool seguridad_habilitada;              // Si el sistema de seguridad está activo
    bool modo_paranoia;                     // Modo alta seguridad (más estricto)
    uint32_t max_intentos_autenticacion;    // Máximo intentos antes de bloqueo
    uint32_t tiempo_bloqueo_ms;             // Tiempo de bloqueo por seguridad
    uint32_t tiempo_sesion_max_ms;          // Tiempo máximo de sesión
    
    // Configuración de proximidad
    proximidad_config_t config_proximidad;
    
    // Configuración anti-jamming
    bool anti_jamming_habilitado;           // Si anti-jamming está activo
    uint32_t tiempo_deteccion_jamming_ms;   // Tiempo para detectar jamming
    uint8_t canales_backup[SEGURIDAD_CANALES_BACKUP]; // Canales de respaldo
    
    // Configuración de respuesta
    bool respuesta_automatica;              // Respuesta automática a amenazas
    amenaza_nivel_t umbral_alarma;          // Umbral para activar alarma
    amenaza_nivel_t umbral_panico;          // Umbral para activar pánico
    
} seguridad_config_t;

// Estructura principal del módulo de seguridad
typedef struct {
    // Estado y configuración
    seguridad_estado_t estado_actual;       // Estado actual del sistema
    seguridad_config_t configuracion;       // Configuración del sistema
    sesion_seguridad_t sesion_actual;       // Información de sesión activa
    autenticacion_stats_t estadisticas;     // Estadísticas de autenticación
    
    // Control de amenazas
    amenaza_tipo_t amenazas_detectadas;     // Máscara de amenazas activas
    amenaza_nivel_t nivel_amenaza_global;   // Nivel de amenaza global
    uint64_t timestamp_ultima_amenaza;      // Timestamp de última amenaza
    
    // Control de hilo y sincronización
    TaskHandle_t task_handle;               // Handle de tarea de seguridad
    SemaphoreHandle_t mutex_seguridad;      // Mutex para acceso thread-safe
    QueueHandle_t cola_eventos_seguridad;   // Cola de eventos de seguridad
    EventGroupHandle_t eventos_sistema;     // Eventos del sistema
    
    // Timers
    esp_timer_handle_t timer_sesion;        // Timer de timeout de sesión
    esp_timer_handle_t timer_heartbeat;     // Timer de heartbeat
    esp_timer_handle_t timer_anti_jamming;  // Timer de detección anti-jamming
    
    // Control de estado
    bool inicializado;                      // Si está inicializado
    bool monitoreo_activo;                  // Si el monitoreo está activo
    uint64_t timestamp_inicializacion;     // Timestamp de inicialización
    
} vehiculo_seguridad_t;

// Estructura de evento de seguridad
typedef struct {
    amenaza_tipo_t tipo_amenaza;            // Tipo de amenaza detectada
    amenaza_nivel_t nivel_amenaza;          // Nivel de la amenaza
    uint64_t timestamp;                     // Timestamp del evento
    int8_t rssi_asociado;                   // RSSI asociado (si aplicable)
    uint32_t datos_adicionales;             // Datos adicionales específicos
    char descripcion[64];                   // Descripción textual del evento
} evento_seguridad_t;

// ============================================================================
// PROTOTIPOS DE FUNCIONES PÚBLICAS
// ============================================================================

/**
 * @brief Inicializa el módulo de seguridad del vehículo
 * @return ESP_OK si éxito, código de error si falla
 */
esp_err_t vehiculo_seguridad_init(void);

/**
 * @brief Desinicializa el módulo de seguridad
 * @return ESP_OK si éxito
 */
esp_err_t vehiculo_seguridad_deinit(void);

/**
 * @brief Inicia el sistema de monitoreo de seguridad
 * @return ESP_OK si éxito, código de error si falla
 */
esp_err_t vehiculo_seguridad_iniciar_monitoreo(void);

/**
 * @brief Detiene el sistema de monitoreo de seguridad
 * @return ESP_OK si éxito
 */
esp_err_t vehiculo_seguridad_detener_monitoreo(void);

/**
 * @brief Procesa un intento de autenticación
 * @param datos_autenticacion Datos del mensaje de autenticación
 * @param tamaño_datos Tamaño de los datos
 * @param rssi RSSI del mensaje recibido
 * @return ESP_OK si autenticación exitosa, código de error si falla
 */
esp_err_t vehiculo_seguridad_procesar_autenticacion(const uint8_t* datos_autenticacion,
                                                   size_t tamaño_datos,
                                                   int8_t rssi);

/**
 * @brief Valida la proximidad de la llave usando múltiples métodos
 * @param rssi RSSI medido
 * @param tiempo_respuesta_us Tiempo de respuesta en microsegundos
 * @param canal_comunicacion Canal de comunicación utilizado
 * @return true si proximidad válida, false si sospechosa
 */
bool vehiculo_seguridad_validar_proximidad(int8_t rssi, 
                                          uint32_t tiempo_respuesta_us,
                                          uint8_t canal_comunicacion);

/**
 * @brief Detecta posibles ataques de relay
 * @param tiempo_respuesta_us Tiempo de respuesta del comando
 * @param rssi RSSI medido
 * @param numero_saltos_frecuencia Número de saltos de frecuencia detectados
 * @return true si posible ataque relay detectado
 */
bool vehiculo_seguridad_detectar_relay_attack(uint32_t tiempo_respuesta_us,
                                             int8_t rssi,
                                             uint8_t numero_saltos_frecuencia);

/**
 * @brief Detecta interferencia/jamming de comunicación
 * @return true si jamming detectado, false en caso contrario
 */
bool vehiculo_seguridad_detectar_jamming(void);

/**
 * @brief Procesa un heartbeat de la llave
 * @param datos_heartbeat Datos del heartbeat
 * @param tamaño_datos Tamaño de los datos
 * @param rssi RSSI del mensaje
 * @return ESP_OK si heartbeat válido
 */
esp_err_t vehiculo_seguridad_procesar_heartbeat(const uint8_t* datos_heartbeat,
                                               size_t tamaño_datos,
                                               int8_t rssi);

/**
 * @brief Reporta una amenaza detectada por sensores externos
 * @param tipo_amenaza Tipo de amenaza detectada
 * @param nivel_amenaza Nivel de severidad
 * @param datos_adicionales Datos adicionales de contexto
 * @return ESP_OK si amenaza procesada correctamente
 */
esp_err_t vehiculo_seguridad_reportar_amenaza(amenaza_tipo_t tipo_amenaza,
                                             amenaza_nivel_t nivel_amenaza,
                                             uint32_t datos_adicionales);

/**
 * @brief Obtiene el estado actual de seguridad
 * @param estado Puntero para retornar el estado actual
 * @return ESP_OK si éxito
 */
esp_err_t vehiculo_seguridad_obtener_estado(seguridad_estado_t* estado);

/**
 * @brief Obtiene el nivel de amenaza actual
 * @param nivel_amenaza Puntero para retornar el nivel de amenaza
 * @param amenazas_activas Puntero para retornar máscara de amenazas activas
 * @return ESP_OK si éxito
 */
esp_err_t vehiculo_seguridad_obtener_nivel_amenaza(amenaza_nivel_t* nivel_amenaza,
                                                  amenaza_tipo_t* amenazas_activas);

/**
 * @brief Obtiene estadísticas de autenticación
 * @param stats Puntero para retornar las estadísticas
 * @return ESP_OK si éxito
 */
esp_err_t vehiculo_seguridad_obtener_estadisticas(autenticacion_stats_t* stats);

/**
 * @brief Obtiene el próximo evento de seguridad de la cola
 * @param evento Puntero para retornar el evento
 * @param timeout_ms Timeout en ms
 * @return ESP_OK si evento recibido, ESP_ERR_TIMEOUT si timeout
 */
esp_err_t vehiculo_seguridad_obtener_evento(evento_seguridad_t* evento, uint32_t timeout_ms);

/**
 * @brief Fuerza el cierre de sesión de seguridad
 * @param motivo Motivo del cierre de sesión
 * @return ESP_OK si éxito
 */
esp_err_t vehiculo_seguridad_cerrar_sesion(const char* motivo);

/**
 * @brief Activa modo de emergencia
 * @param tipo_emergencia Tipo de emergencia detectada
 * @return ESP_OK si éxito
 */
esp_err_t vehiculo_seguridad_activar_emergencia(amenaza_tipo_t tipo_emergencia);

/**
 * @brief Configura los parámetros de seguridad
 * @param nueva_config Nueva configuración de seguridad
 * @return ESP_OK si configuración aplicada correctamente
 */
esp_err_t vehiculo_seguridad_configurar(const seguridad_config_t* nueva_config);

/**
 * @brief Ejecuta verificación de integridad del sistema de seguridad
 * @param reporte Buffer para generar reporte de integridad
 * @param tamaño_buffer Tamaño del buffer de reporte
 * @return true si sistema íntegro, false si hay problemas
 */
bool vehiculo_seguridad_verificar_integridad(char* reporte, size_t tamaño_buffer);

/**
 * @brief Genera un reporte completo del estado de seguridad
 * @param buffer Buffer para el reporte
 * @param tamaño_buffer Tamaño del buffer
 * @return Número de bytes escritos en el buffer
 */
size_t vehiculo_seguridad_generar_reporte_estado(char* buffer, size_t tamaño_buffer);

/**
 * @brief Obtiene la instancia global del módulo de seguridad
 * @return Puntero a la estructura de seguridad (solo lectura)
 */
const vehiculo_seguridad_t* vehiculo_seguridad_obtener_instancia(void);

// ============================================================================
// FUNCIONES DE UTILIDAD
// ============================================================================

/**
 * @brief Convierte estado de seguridad a string
 * @param estado Estado de seguridad
 * @return String descriptivo del estado
 */
const char* vehiculo_seguridad_estado_a_string(seguridad_estado_t estado);

/**
 * @brief Convierte nivel de amenaza a string
 * @param nivel Nivel de amenaza
 * @return String descriptivo del nivel
 */
const char* vehiculo_seguridad_nivel_amenaza_a_string(amenaza_nivel_t nivel);

/**
 * @brief Convierte tipo de amenaza a string
 * @param tipo Tipo de amenaza
 * @return String descriptivo del tipo de amenaza
 */
const char* vehiculo_seguridad_tipo_amenaza_a_string(amenaza_tipo_t tipo);

#ifdef __cplusplus
}
#endif

#endif // VEHICULO_SEGURIDAD_H
