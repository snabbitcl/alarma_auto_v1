#ifndef PROTOCOLO_SEGURO_H
#define PROTOCOLO_SEGURO_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Versión del protocolo
#define PROTOCOLO_VERSION           1
#define PROTOCOLO_MAGIC             0xAB12CD34

// Configuración AU915
#define CANAL_BASE_AU915            915000000
#define CANALES_TOTALES             64
#define ANCHO_BANDA_HZ              125000
#define POTENCIA_MAX_DBM            20  // 100mW EIRP

// Tamaños de mensaje
#define PAYLOAD_MAX_SIZE            48
#define HMAC_SIZE                   32
#define AES_KEY_SIZE                16
#define ROLLING_CODE_SIZE           32

// Tipos de mensaje
typedef enum {
    // Comandos básicos
    MSG_COMANDO = 0x01,
    MSG_RESPUESTA = 0x02,
    MSG_ESTADO = 0x03,
    MSG_ALERTA = 0x04,
    
    // Gestión de sesión
    MSG_HANDSHAKE = 0x10,
    MSG_CHALLENGE = 0x11,
    MSG_RESPONSE = 0x12,
    MSG_KEEPALIVE = 0x13,
    
    // Telemetría
    MSG_TELEMETRIA = 0x20,
    MSG_DIAGNOSTICO = 0x21,
    MSG_LOG_EVENTO = 0x22,
    
    // OTA
    MSG_OTA_INICIO = 0x30,
    MSG_OTA_BLOQUE = 0x31,
    MSG_OTA_FIN = 0x32
} tipo_mensaje_t;

// Comandos del vehículo
typedef enum {
    CMD_BLOQUEAR = 0x01,
    CMD_DESBLOQUEAR = 0x02,
    CMD_ARRANQUE = 0x03,
    CMD_PANICO = 0x04,
    CMD_BUSCAR = 0x05,
    CMD_ESTADO = 0x06,
    CMD_CONFIG = 0x07
} comando_vehiculo_t;

// Estructura del mensaje seguro
typedef struct __attribute__((packed)) {
    // Header no cifrado
    uint32_t magic;             // PROTOCOLO_MAGIC
    uint8_t  version;           // Versión protocolo
    uint8_t  tipo_mensaje;      // tipo_mensaje_t
    uint16_t longitud;          // Longitud total del mensaje
    uint32_t contador;          // Anti-replay counter
    
    // Payload cifrado AES-128-CTR
    struct {
        uint32_t timestamp;     // Tiempo UNIX en ms
        uint8_t  comando;       // Comando específico
        uint8_t  datos[PAYLOAD_MAX_SIZE]; // Datos del comando
        uint32_t crc32;         // CRC del payload
    } payload_cifrado;
    
    // Autenticación
    uint8_t hmac[HMAC_SIZE];   // HMAC-SHA256 de todo el mensaje
} mensaje_seguro_t;

// Estructura de respuesta
typedef struct __attribute__((packed)) {
    uint8_t resultado;          // 0=éxito, >0=código error
    uint32_t contador_respuesta;
    uint8_t datos_respuesta[16];
} respuesta_comando_t;

// Estructura de telemetría del vehículo
typedef struct __attribute__((packed)) {
    // Estado vehículo
    uint8_t puertas;            // Bitmap puertas
    uint8_t ventanas;           // Bitmap ventanas
    uint16_t voltaje_bateria;   // Voltaje en mV
    int8_t temperatura;         // Temperatura en Celsius
    
    // Sensores
    uint8_t alarma_activa;      // Estado alarma
    uint8_t movimiento;         // Sensor movimiento
    int8_t rssi_lora;          // Calidad señal LoRa
    
    // Contadores
    uint32_t tiempo_encendido;  // Tiempo encendido en segundos
    uint16_t comandos_recibidos; // Total comandos
} telemetria_vehiculo_t;

// Funciones principales del protocolo
esp_err_t protocolo_init(void);
esp_err_t protocolo_enviar_comando(comando_vehiculo_t cmd, uint8_t* datos, size_t len);
esp_err_t protocolo_procesar_mensaje(uint8_t* buffer, size_t len, mensaje_seguro_t* msg_out);
esp_err_t protocolo_crear_respuesta(respuesta_comando_t* respuesta, uint8_t* buffer_out, size_t* len_out);

// Funciones de sesión
esp_err_t sesion_establecer(void);
esp_err_t sesion_renovar_claves(void);
bool sesion_activa(void);

// Funciones anti-replay
bool validar_contador_replay(uint32_t contador);
void actualizar_ventana_replay(uint32_t contador);

// Utilidades
uint32_t obtener_timestamp_seguro(void);
uint32_t obtener_contador_monotono(void);

#ifdef __cplusplus
}
#endif

#endif // PROTOCOLO_SEGURO_H
