#ifndef CONFIG_AU915_H
#define CONFIG_AU915_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Configuración para cumplimiento SUBTEL Chile AU915
#define AU915_FREQ_MIN          915000000   // 915 MHz
#define AU915_FREQ_MAX          928000000   // 928 MHz
#define AU915_FREQ_BASE         915200000   // Base + offset
#define AU915_CHANNEL_SPACING   200000      // 200 kHz
#define AU915_CHANNELS_TOTAL    64          // Canales disponibles

// Configuración de spreading factor
#define AU915_SF_MIN            7
#define AU915_SF_MAX            12
#define AU915_SF_DEFAULT        8

// Ancho de banda
#define AU915_BW_125KHZ         0
#define AU915_BW_250KHZ         1
#define AU915_BW_500KHZ         2
#define AU915_BW_DEFAULT        AU915_BW_125KHZ

// Potencia de transmisión
#define AU915_POWER_MIN_DBM     2
#define AU915_POWER_MAX_DBM     20      // 100mW EIRP
#define AU915_POWER_DEFAULT     17      // ~50mW para margen

// Duty cycle limits (SUBTEL compliance)
#define AU915_DUTY_CYCLE_MAX    10      // 10% máximo
#define AU915_DWELL_TIME_MS     400     // Tiempo máximo por canal

// Estructura de configuración de canal
typedef struct {
    uint32_t frecuencia;        // Frecuencia en Hz
    uint8_t spreading_factor;   // SF 7-12
    uint8_t ancho_banda;        // BW index
    int8_t potencia_dbm;        // Potencia en dBm
    bool permitido;             // Canal permitido por regulación
} canal_au915_t;

// Tabla de canales AU915
extern const canal_au915_t canales_au915[AU915_CHANNELS_TOTAL];

// Funciones de configuración
uint32_t au915_obtener_frecuencia(uint8_t canal);
bool au915_canal_valido(uint8_t canal);
int8_t au915_potencia_maxima_canal(uint8_t canal);
uint32_t au915_tiempo_aire_ms(uint8_t sf, uint8_t bw, uint8_t payload_len);

// Verificación de cumplimiento regulatorio
bool au915_verificar_duty_cycle(uint8_t canal, uint32_t tiempo_tx_ms);
bool au915_verificar_potencia(int8_t potencia_dbm);
bool au915_verificar_dwell_time(uint32_t tiempo_ms);

// FHSS - Frequency Hopping Spread Spectrum
typedef struct {
    uint8_t secuencia[AU915_CHANNELS_TOTAL];
    uint8_t indice_actual;
    uint32_t semilla;
    uint32_t ultimo_salto_ms;
} fhss_config_t;

// Funciones FHSS
void fhss_init(fhss_config_t* config, uint32_t semilla);
uint8_t fhss_siguiente_canal(fhss_config_t* config);
bool fhss_puede_transmitir(fhss_config_t* config, uint8_t canal);

#ifdef __cplusplus
}
#endif

#endif // CONFIG_AU915_H
