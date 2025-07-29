/**
 * @file vehiculo_sensores.h
 * @brief Sistema de sensores del vehículo para detección de intrusiones
 * 
 * Monitoreo integral de sensores digitales y analógicos con filtrado
 * de ruido, debounce automático y detección de patrones sospechosos
 * 
 * @author Sistema Alarma Vehicular
 * @date 2024
 */

#ifndef VEHICULO_SENSORES_H
#define VEHICULO_SENSORES_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// DEFINICIONES Y CONSTANTES
// ============================================================================

#define SENSORES_TAG "VEH_SENSORES"

// Configuración de muestreo
#define SENSORES_INTERVALO_LECTURA_MS       50      // 20Hz de muestreo
#define SENSORES_DEBOUNCE_TIME_MS           100     // Anti-rebote de 100ms
#define SENSORES_FILTRO_VENTANA_MUESTRAS    10      // Ventana deslizante para filtro
#define SENSORES_UMBRAL_BATERIA_CRITICA_MV  10000   // 10V crítico
#define SENSORES_UMBRAL_BATERIA_BAJA_MV     11000   // 11V advertencia

// Configuración ADC
#define SENSORES_ADC_ATTEN              ADC_ATTEN_DB_12    // 0-3.3V con divisor (nueva API)
#define SENSORES_ADC_BITWIDTH           ADC_BITWIDTH_12    // 12 bits de resolución (nueva API)
#define SENSORES_ADC_SAMPLES_AVG        8                  // Promedio de 8 muestras

// Estados de sensores
typedef enum {
    SENSOR_ESTADO_NORMAL = 0,       // Funcionamiento normal
    SENSOR_ESTADO_ACTIVADO,         // Sensor detecta evento
    SENSOR_ESTADO_ERROR,            // Error de lectura
    SENSOR_ESTADO_DESCONECTADO      // Sensor desconectado/falla hardware
} sensor_estado_t;

// Tipos de sensores digitales
typedef enum {
    SENSOR_PUERTA_CONDUCTOR = 0,    // Reed switch puerta conductor
    SENSOR_PUERTA_PASAJERO,         // Reed switch puerta pasajero
    SENSOR_CAPO,                    // Reed switch capó
    SENSOR_BAUL,                    // Reed switch baúl
    SENSOR_MOVIMIENTO_PIR,          // PIR detector de movimiento interior
    SENSOR_SHOCK,                   // Sensor de vibración/impacto
    SENSOR_TAMPER_CAJA,             // Tamper switch de la caja del módulo
    SENSOR_DIGITAL_MAX_COUNT
} sensor_digital_tipo_t;

// Tipos de sensores analógicos
typedef enum {
    SENSOR_BATERIA_VOLTAJE = 0,     // Monitor de voltaje de batería
    SENSOR_CORRIENTE_SISTEMA,       // Monitor de corriente del sistema
    SENSOR_TEMPERATURA_INTERNA,     // Temperatura interna del módulo
    SENSOR_ANALOGICO_MAX_COUNT
} sensor_analogico_tipo_t;

// Configuración de sensor digital
typedef struct {
    gpio_num_t pin;                 // Pin GPIO del sensor
    bool activo_alto;               // true = activo en alto, false = activo en bajo
    bool pull_up;                   // Habilitar pull-up interno
    uint32_t debounce_ms;           // Tiempo de anti-rebote específico
    const char* nombre;             // Nombre descriptivo del sensor
    bool critico;                   // true si es crítico para seguridad
} sensor_digital_config_t;

// Configuración de sensor analógico
typedef struct {
    adc_channel_t canal_adc;        // Canal ADC utilizado
    uint32_t factor_escala;         // Factor de escala para conversión
    uint32_t offset_mv;             // Offset en mV
    uint32_t umbral_bajo_mv;        // Umbral de valor bajo
    uint32_t umbral_alto_mv;        // Umbral de valor alto
    const char* nombre;             // Nombre descriptivo del sensor
    const char* unidad;             // Unidad de medida (V, mA, °C)
} sensor_analogico_config_t;

// Estado runtime de sensor digital
typedef struct {
    sensor_estado_t estado;         // Estado actual del sensor
    bool valor_actual;              // Valor actual filtrado
    bool valor_anterior;            // Valor anterior para detección de cambios
    uint64_t timestamp_ultimo_cambio; // Timestamp del último cambio
    uint64_t tiempo_activacion_total; // Tiempo total activado
    uint32_t conteo_activaciones;   // Número de activaciones
    uint32_t conteo_errores;        // Número de errores de lectura
    bool debounce_activo;           // Si está en período de anti-rebote
    uint64_t timestamp_debounce;    // Timestamp para control de debounce
} sensor_digital_runtime_t;

// Estado runtime de sensor analógico
typedef struct {
    sensor_estado_t estado;         // Estado actual del sensor
    uint32_t valor_actual_mv;       // Valor actual en mV
    uint32_t valor_filtrado_mv;     // Valor filtrado
    uint32_t valor_minimo_mv;       // Valor mínimo registrado
    uint32_t valor_maximo_mv;       // Valor máximo registrado
    uint32_t muestras_buffer[SENSORES_FILTRO_VENTANA_MUESTRAS]; // Buffer circular para filtro
    uint8_t indice_buffer;          // Índice actual en buffer circular
    uint64_t timestamp_ultima_lectura; // Timestamp de última lectura
    uint32_t conteo_lecturas;       // Número total de lecturas
    uint32_t conteo_errores;        // Número de errores de lectura
} sensor_analogico_runtime_t;

// Eventos de sensores
typedef enum {
    SENSOR_EVENTO_PUERTA_ABIERTA = BIT0,
    SENSOR_EVENTO_CAPO_ABIERTO = BIT1,
    SENSOR_EVENTO_BAUL_ABIERTO = BIT2,
    SENSOR_EVENTO_MOVIMIENTO_DETECTADO = BIT3,
    SENSOR_EVENTO_SHOCK_DETECTADO = BIT4,
    SENSOR_EVENTO_TAMPER_DETECTADO = BIT5,
    SENSOR_EVENTO_BATERIA_BAJA = BIT6,
    SENSOR_EVENTO_BATERIA_CRITICA = BIT7,
    SENSOR_EVENTO_TEMPERATURA_ALTA = BIT8,
    SENSOR_EVENTO_ERROR_SENSOR = BIT9
} sensor_eventos_t;

// Estructura principal del módulo de sensores
typedef struct {
    // Configuración
    sensor_digital_config_t config_digital[SENSOR_DIGITAL_MAX_COUNT];
    sensor_analogico_config_t config_analogico[SENSOR_ANALOGICO_MAX_COUNT];
    
    // Estado runtime
    sensor_digital_runtime_t runtime_digital[SENSOR_DIGITAL_MAX_COUNT];
    sensor_analogico_runtime_t runtime_analogico[SENSOR_ANALOGICO_MAX_COUNT];
    
    // Control de hilo y sincronización
    TaskHandle_t task_handle;
    SemaphoreHandle_t mutex_sensores;
    QueueHandle_t cola_eventos;
    
    // Configuración general
    bool inicializado;
    bool monitoreo_activo;
    uint32_t intervalo_lectura_ms;
    uint64_t timestamp_ultima_lectura_global;
    
    // Estadísticas
    uint64_t conteo_lecturas_totales;
    uint32_t conteo_eventos_totales;
    uint32_t conteo_errores_totales;
    
    // Filtro de ruido y detección de patrones
    bool filtro_ruido_habilitado;
    uint32_t umbral_ruido;
    
} vehiculo_sensores_t;

// Estructura de evento de sensor
typedef struct {
    sensor_eventos_t tipo_evento;
    uint64_t timestamp;
    union {
        struct {
            sensor_digital_tipo_t tipo_sensor;
            bool valor;
        } digital;
        struct {
            sensor_analogico_tipo_t tipo_sensor;
            uint32_t valor_mv;
        } analogico;
    } datos;
} sensor_evento_t;

// ============================================================================
// PROTOTIPOS DE FUNCIONES PÚBLICAS
// ============================================================================

/**
 * @brief Inicializa el módulo de sensores del vehículo
 * @return ESP_OK si éxito, código de error si falla
 */
esp_err_t vehiculo_sensores_init(void);

/**
 * @brief Desinicializa el módulo de sensores
 * @return ESP_OK si éxito
 */
esp_err_t vehiculo_sensores_deinit(void);

/**
 * @brief Inicia el monitoreo activo de sensores
 * @return ESP_OK si éxito, código de error si falla
 */
esp_err_t vehiculo_sensores_iniciar_monitoreo(void);

/**
 * @brief Detiene el monitoreo activo de sensores
 * @return ESP_OK si éxito
 */
esp_err_t vehiculo_sensores_detener_monitoreo(void);

/**
 * @brief Lee el estado actual de un sensor digital
 * @param tipo Tipo de sensor digital
 * @param estado Puntero para retornar el estado actual
 * @return ESP_OK si éxito, código de error si falla
 */
esp_err_t vehiculo_sensor_digital_leer(sensor_digital_tipo_t tipo, bool* estado);

/**
 * @brief Lee el valor actual de un sensor analógico
 * @param tipo Tipo de sensor analógico
 * @param valor_mv Puntero para retornar el valor en mV
 * @return ESP_OK si éxito, código de error si falla
 */
esp_err_t vehiculo_sensor_analogico_leer(sensor_analogico_tipo_t tipo, uint32_t* valor_mv);

/**
 * @brief Obtiene el estado general de todos los sensores
 * @param estado_digital Array para retornar estados de sensores digitales
 * @param valores_analogicos_mv Array para retornar valores analógicos en mV
 * @return ESP_OK si éxito
 */
esp_err_t vehiculo_sensores_obtener_estado_completo(bool estado_digital[SENSOR_DIGITAL_MAX_COUNT],
                                                   uint32_t valores_analogicos_mv[SENSOR_ANALOGICO_MAX_COUNT]);

/**
 * @brief Configura los umbrales de un sensor analógico
 * @param tipo Tipo de sensor analógico
 * @param umbral_bajo_mv Nuevo umbral bajo en mV
 * @param umbral_alto_mv Nuevo umbral alto en mV
 * @return ESP_OK si éxito, código de error si falla
 */
esp_err_t vehiculo_sensor_analogico_configurar_umbrales(sensor_analogico_tipo_t tipo,
                                                       uint32_t umbral_bajo_mv,
                                                       uint32_t umbral_alto_mv);

/**
 * @brief Configura el tiempo de debounce de un sensor digital
 * @param tipo Tipo de sensor digital
 * @param debounce_ms Nuevo tiempo de debounce en ms
 * @return ESP_OK si éxito, código de error si falla
 */
esp_err_t vehiculo_sensor_digital_configurar_debounce(sensor_digital_tipo_t tipo,
                                                     uint32_t debounce_ms);

/**
 * @brief Obtiene estadísticas de un sensor digital
 * @param tipo Tipo de sensor digital
 * @param conteo_activaciones Puntero para retornar número de activaciones
 * @param tiempo_total_activo_ms Puntero para retornar tiempo total activo en ms
 * @param conteo_errores Puntero para retornar número de errores
 * @return ESP_OK si éxito, código de error si falla
 */
esp_err_t vehiculo_sensor_digital_obtener_estadisticas(sensor_digital_tipo_t tipo,
                                                      uint32_t* conteo_activaciones,
                                                      uint64_t* tiempo_total_activo_ms,
                                                      uint32_t* conteo_errores);

/**
 * @brief Obtiene estadísticas de un sensor analógico
 * @param tipo Tipo de sensor analógico
 * @param valor_actual_mv Puntero para retornar valor actual en mV
 * @param valor_minimo_mv Puntero para retornar valor mínimo registrado en mV
 * @param valor_maximo_mv Puntero para retornar valor máximo registrado en mV
 * @param conteo_lecturas Puntero para retornar número de lecturas
 * @return ESP_OK si éxito, código de error si falla
 */
esp_err_t vehiculo_sensor_analogico_obtener_estadisticas(sensor_analogico_tipo_t tipo,
                                                        uint32_t* valor_actual_mv,
                                                        uint32_t* valor_minimo_mv,
                                                        uint32_t* valor_maximo_mv,
                                                        uint32_t* conteo_lecturas);

/**
 * @brief Obtiene el próximo evento de la cola de eventos
 * @param evento Puntero para retornar el evento
 * @param timeout_ms Timeout en ms (0 = no bloqueante, portMAX_DELAY = espera indefinida)
 * @return ESP_OK si evento recibido, ESP_ERR_TIMEOUT si timeout
 */
esp_err_t vehiculo_sensores_obtener_evento(sensor_evento_t* evento, uint32_t timeout_ms);

/**
 * @brief Verifica la integridad del sistema de sensores
 * @param reporte Buffer para generar reporte de integridad
 * @param tamaño_buffer Tamaño del buffer de reporte
 * @return true si sistema íntegro, false si hay problemas
 */
bool vehiculo_sensores_verificar_integridad(char* reporte, size_t tamaño_buffer);

/**
 * @brief Ejecuta calibración automática de sensores analógicos
 * @return ESP_OK si calibración exitosa, código de error si falla
 */
esp_err_t vehiculo_sensores_calibrar_analogicos(void);

/**
 * @brief Ejecuta autotest de todos los sensores
 * @return ESP_OK si todos los tests pasaron, ESP_FAIL si alguno falló
 */
esp_err_t vehiculo_sensores_autotest(void);

/**
 * @brief Habilita/deshabilita el filtro de ruido
 * @param habilitar true para habilitar, false para deshabilitar
 * @param umbral_ruido Umbral de ruido en unidades del sensor
 * @return ESP_OK si éxito
 */
esp_err_t vehiculo_sensores_configurar_filtro_ruido(bool habilitar, uint32_t umbral_ruido);

/**
 * @brief Genera un reporte completo del estado de sensores
 * @param buffer Buffer para el reporte
 * @param tamaño_buffer Tamaño del buffer
 * @return Número de bytes escritos en el buffer
 */
size_t vehiculo_sensores_generar_reporte_estado(char* buffer, size_t tamaño_buffer);

/**
 * @brief Obtiene la instancia global del módulo de sensores
 * @return Puntero a la estructura de sensores (solo lectura)
 */
const vehiculo_sensores_t* vehiculo_sensores_obtener_instancia(void);

// ============================================================================
// FUNCIONES DE UTILIDAD
// ============================================================================

/**
 * @brief Convierte tipo de sensor digital a string
 * @param tipo Tipo de sensor digital
 * @return String descriptivo del sensor
 */
const char* vehiculo_sensor_digital_tipo_a_string(sensor_digital_tipo_t tipo);

/**
 * @brief Convierte tipo de sensor analógico a string
 * @param tipo Tipo de sensor analógico
 * @return String descriptivo del sensor
 */
const char* vehiculo_sensor_analogico_tipo_a_string(sensor_analogico_tipo_t tipo);

/**
 * @brief Convierte estado de sensor a string
 * @param estado Estado del sensor
 * @return String descriptivo del estado
 */
const char* vehiculo_sensor_estado_a_string(sensor_estado_t estado);

/**
 * @brief Convierte tipo de evento a string
 * @param evento Tipo de evento
 * @return String descriptivo del evento
 */
const char* vehiculo_sensor_evento_a_string(sensor_eventos_t evento);

// ============================================================================
// ESTRUCTURAS DE COMPATIBILIDAD CON MAIN
// ============================================================================

/**
 * @brief Estructura de configuración simplificada para compatibilidad con main
 */
typedef struct {
    gpio_num_t pin_puerta_conductor;
    gpio_num_t pin_puerta_pasajero;
    gpio_num_t pin_capo;
    gpio_num_t pin_baul;
    gpio_num_t pin_movimiento_pir;
    gpio_num_t pin_shock_sensor;
    gpio_num_t pin_bateria_monitor;
    adc_channel_t adc_bateria;
    gpio_num_t pin_tamper_caja;
    uint32_t umbral_bateria_baja_mv;    // Voltaje mínimo
    uint32_t sensibilidad_shock;        // Umbral de vibración
} sensor_config_t;

/**
 * @brief Estado actual de sensores simplificado para compatibilidad con main
 */
typedef struct {
    bool puerta_conductor;
    bool puerta_pasajero;
    bool capo;
    bool baul;
    bool movimiento_detectado;
    bool shock_detectado;
    bool tamper_detectado;
    uint32_t voltaje_bateria_mv;
    bool bateria_baja;
    uint64_t timestamp_ultima_lectura;
} sensor_estado_simplificado_t;

#ifdef __cplusplus
}
#endif

#endif // VEHICULO_SENSORES_H
