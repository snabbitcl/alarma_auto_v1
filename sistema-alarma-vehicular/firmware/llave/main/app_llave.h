#ifndef APP_LLAVE_H
#define APP_LLAVE_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

// Versión del firmware
#define FIRMWARE_VERSION_MAJOR  1
#define FIRMWARE_VERSION_MINOR  0
#define FIRMWARE_VERSION_PATCH  0

// Estados principales de la llave
typedef enum {
    LLAVE_ESTADO_ARRANQUE,
    LLAVE_ESTADO_BLOQUEADA,
    LLAVE_ESTADO_ACTIVA,
    LLAVE_ESTADO_CONFIGURACION,
    LLAVE_ESTADO_ACTUALIZACION,
    LLAVE_ESTADO_ERROR,
    LLAVE_ESTADO_SLEEP
} llave_estado_t;

// Eventos del sistema
typedef enum {
    EVENTO_BOTON_PRESIONADO,
    EVENTO_COMANDO_VEHICULO,
    EVENTO_RESPUESTA_RECIBIDA,
    EVENTO_TIMEOUT_CONEXION,
    EVENTO_BATERIA_BAJA,
    EVENTO_ERROR_COMUNICACION,
    EVENTO_SLEEP_REQUEST
} llave_evento_t;

// Estructura del evento
typedef struct {
    llave_evento_t tipo;
    uint32_t param1;
    uint32_t param2;
    void* data;
} llave_evento_msg_t;

// Configuración de pines
#define PIN_SPI3_MISO   13
#define PIN_SPI3_MOSI   11
#define PIN_SPI3_CLK    12
#define PIN_TFT_CS      10
#define PIN_TFT_DC      9
#define PIN_TFT_RST     8
#define PIN_TFT_BL      7

#define PIN_SPI2_MISO   37
#define PIN_SPI2_MOSI   35
#define PIN_SPI2_CLK    36
#define PIN_LORA_CS     34
#define PIN_LORA_RST    33
#define PIN_LORA_IRQ    38

#define PIN_BOTON_1     0
#define PIN_BOTON_2     45
#define PIN_BOTON_3     46
#define PIN_BOTON_4     47

#define PIN_LED_STATUS  48
#define PIN_BAT_ADC     4

// Prioridades de tareas
#define PRIORIDAD_GUI_TASK          5
#define PRIORIDAD_BLE_TASK          6
#define PRIORIDAD_LORA_TX_TASK      7
#define PRIORIDAD_LORA_RX_TASK      8
#define PRIORIDAD_POWER_TASK        4
#define PRIORIDAD_SECURITY_TASK     9
#define PRIORIDAD_MAIN_TASK         3

// Tamaños de stack
#define STACK_GUI_TASK              8192
#define STACK_BLE_TASK              4096
#define STACK_LORA_TASK             4096
#define STACK_POWER_TASK            3072
#define STACK_SECURITY_TASK         3072
#define STACK_MAIN_TASK             4096

// Handles de tareas
extern TaskHandle_t task_gui_handle;
extern TaskHandle_t task_ble_handle;
extern TaskHandle_t task_lora_tx_handle;
extern TaskHandle_t task_lora_rx_handle;
extern TaskHandle_t task_power_handle;
extern TaskHandle_t task_security_handle;

// Colas y semáforos
extern QueueHandle_t cola_eventos_principales;
extern QueueHandle_t cola_comandos_lora;
extern SemaphoreHandle_t mutex_display;
extern SemaphoreHandle_t mutex_lora;

// Funciones principales
esp_err_t app_llave_init(void);
esp_err_t app_llave_start_tasks(void);
void app_llave_main_loop(void);
esp_err_t app_llave_shutdown(void);

// Funciones de tareas
void tarea_gui_principal(void* pvParameters);
void tarea_ble_cliente(void* pvParameters);
void tarea_lora_transceptor(void* pvParameters);
void tarea_gestion_energia(void* pvParameters);
void tarea_monitor_seguridad(void* pvParameters);

// Funciones de gestión de estado
llave_estado_t obtener_estado_actual(void);
esp_err_t cambiar_estado(llave_estado_t nuevo_estado);
bool puede_cambiar_estado(llave_estado_t desde, llave_estado_t hacia);

// Funciones de eventos
esp_err_t enviar_evento(llave_evento_t tipo, uint32_t param1, uint32_t param2, void* data);
esp_err_t procesar_evento(llave_evento_msg_t* evento);

// Funciones de comunicación
esp_err_t enviar_comando_vehiculo(uint8_t comando, uint8_t* datos, size_t len);
esp_err_t establecer_conexion_vehiculo(void);
bool vehiculo_conectado(void);

// Funciones de configuración
esp_err_t guardar_configuracion(void);
esp_err_t cargar_configuracion(void);
esp_err_t reset_configuracion(void);

// Funciones de diagnóstico
uint32_t obtener_tiempo_uptime(void);
uint8_t obtener_nivel_bateria(void);
int8_t obtener_rssi_ultimo(void);
uint32_t obtener_comandos_enviados(void);

// Funciones de limpieza
esp_err_t app_llave_deinit_adc(void);

#ifdef __cplusplus
}
#endif

#endif // APP_LLAVE_H
