#include "config_au915.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "AU915";

// Tabla de canales AU915 conforme a SUBTEL Chile
const canal_au915_t canales_au915[AU915_CHANNELS_TOTAL] = {
    // Canales 0-7: 915.2 - 916.6 MHz (200 kHz spacing)
    {915200000, 7, AU915_BW_125KHZ, 20, true},
    {915400000, 7, AU915_BW_125KHZ, 20, true},
    {915600000, 7, AU915_BW_125KHZ, 20, true},
    {915800000, 7, AU915_BW_125KHZ, 20, true},
    {916000000, 8, AU915_BW_125KHZ, 20, true},
    {916200000, 8, AU915_BW_125KHZ, 20, true},
    {916400000, 8, AU915_BW_125KHZ, 20, true},
    {916600000, 8, AU915_BW_125KHZ, 20, true},
    
    // Canales 8-15: 916.8 - 918.2 MHz
    {916800000, 8, AU915_BW_125KHZ, 20, true},
    {917000000, 8, AU915_BW_125KHZ, 20, true},
    {917200000, 9, AU915_BW_125KHZ, 17, true},
    {917400000, 9, AU915_BW_125KHZ, 17, true},
    {917600000, 9, AU915_BW_125KHZ, 17, true},
    {917800000, 9, AU915_BW_125KHZ, 17, true},
    {918000000, 9, AU915_BW_125KHZ, 17, true},
    {918200000, 9, AU915_BW_125KHZ, 17, true},
    
    // Canales 16-23: 918.4 - 919.8 MHz
    {918400000, 9, AU915_BW_125KHZ, 17, true},
    {918600000, 9, AU915_BW_125KHZ, 17, true},
    {918800000, 10, AU915_BW_125KHZ, 14, true},
    {919000000, 10, AU915_BW_125KHZ, 14, true},
    {919200000, 10, AU915_BW_125KHZ, 14, true},
    {919400000, 10, AU915_BW_125KHZ, 14, true},
    {919600000, 10, AU915_BW_125KHZ, 14, true},
    {919800000, 10, AU915_BW_125KHZ, 14, true},
    
    // Canales 24-31: 920.0 - 921.4 MHz
    {920000000, 10, AU915_BW_125KHZ, 14, true},
    {920200000, 10, AU915_BW_125KHZ, 14, true},
    {920400000, 11, AU915_BW_125KHZ, 11, true},
    {920600000, 11, AU915_BW_125KHZ, 11, true},
    {920800000, 11, AU915_BW_125KHZ, 11, true},
    {921000000, 11, AU915_BW_125KHZ, 11, true},
    {921200000, 11, AU915_BW_125KHZ, 11, true},
    {921400000, 11, AU915_BW_125KHZ, 11, true},
    
    // Canales 32-39: 921.6 - 923.0 MHz
    {921600000, 11, AU915_BW_125KHZ, 11, true},
    {921800000, 11, AU915_BW_125KHZ, 11, true},
    {922000000, 12, AU915_BW_125KHZ, 8, true},
    {922200000, 12, AU915_BW_125KHZ, 8, true},
    {922400000, 12, AU915_BW_125KHZ, 8, true},
    {922600000, 12, AU915_BW_125KHZ, 8, true},
    {922800000, 12, AU915_BW_125KHZ, 8, true},
    {923000000, 12, AU915_BW_125KHZ, 8, true},
    
    // Canales 40-47: 923.2 - 924.6 MHz
    {923200000, 12, AU915_BW_125KHZ, 8, true},
    {923400000, 12, AU915_BW_125KHZ, 8, true},
    {923600000, 12, AU915_BW_125KHZ, 8, false}, // Restringido
    {923800000, 12, AU915_BW_125KHZ, 8, false}, // Restringido
    {924000000, 12, AU915_BW_125KHZ, 8, false}, // Restringido
    {924200000, 12, AU915_BW_125KHZ, 8, false}, // Restringido
    {924400000, 12, AU915_BW_125KHZ, 8, false}, // Restringido
    {924600000, 12, AU915_BW_125KHZ, 8, false}, // Restringido
    
    // Canales 48-55: 924.8 - 926.2 MHz
    {924800000, 12, AU915_BW_125KHZ, 8, false}, // Restringido
    {925000000, 12, AU915_BW_125KHZ, 8, false}, // Restringido
    {925200000, 12, AU915_BW_125KHZ, 8, false}, // Restringido
    {925400000, 12, AU915_BW_125KHZ, 8, false}, // Restringido
    {925600000, 12, AU915_BW_125KHZ, 8, false}, // Restringido
    {925800000, 12, AU915_BW_125KHZ, 8, false}, // Restringido
    {926000000, 12, AU915_BW_125KHZ, 8, false}, // Restringido
    {926200000, 12, AU915_BW_125KHZ, 8, false}, // Restringido
    
    // Canales 56-63: 926.4 - 927.8 MHz (algunos permitidos)
    {926400000, 12, AU915_BW_125KHZ, 8, true},
    {926600000, 12, AU915_BW_125KHZ, 8, true},
    {926800000, 12, AU915_BW_125KHZ, 8, true},
    {927000000, 12, AU915_BW_125KHZ, 8, true},
    {927200000, 12, AU915_BW_125KHZ, 8, true},
    {927400000, 12, AU915_BW_125KHZ, 8, true},
    {927600000, 12, AU915_BW_125KHZ, 8, true},
    {927800000, 12, AU915_BW_125KHZ, 8, true}
};

uint32_t au915_obtener_frecuencia(uint8_t canal) {
    if (canal >= AU915_CHANNELS_TOTAL) {
        return 0;
    }
    return canales_au915[canal].frecuencia;
}

bool au915_canal_valido(uint8_t canal) {
    if (canal >= AU915_CHANNELS_TOTAL) {
        return false;
    }
    return canales_au915[canal].permitido;
}

int8_t au915_potencia_maxima_canal(uint8_t canal) {
    if (canal >= AU915_CHANNELS_TOTAL) {
        return AU915_POWER_MIN_DBM;
    }
    return canales_au915[canal].potencia_dbm;
}

uint32_t au915_tiempo_aire_ms(uint8_t sf, uint8_t bw, uint8_t payload_len) {
    // Cálculo simplificado del tiempo en aire para LoRa
    // Fórmula basada en Semtech AN1200.22
    
    if (sf < 7 || sf > 12) {
        return 0;
    }
    
    // Tiempo de símbolo en microsegundos
    uint32_t ts_us;
    switch (bw) {
        case AU915_BW_125KHZ: ts_us = (1000000UL << sf) / 125000; break;
        case AU915_BW_250KHZ: ts_us = (1000000UL << sf) / 250000; break;
        case AU915_BW_500KHZ: ts_us = (1000000UL << sf) / 500000; break;
        default: return 0;
    }
    
    // Preámbulo: 8 símbolos + 4.25 símbolos
    uint32_t tiempo_preambulo_us = (12 * ts_us) + (ts_us / 4);
    
    // Payload
    int payload_symb = 8 + ((payload_len - 4) * 8 + 16 - 4 * sf + 28 + 16) / (4 * sf);
    if (payload_symb < 0) payload_symb = 8;
    
    uint32_t tiempo_payload_us = payload_symb * ts_us;
    
    return (tiempo_preambulo_us + tiempo_payload_us) / 1000; // Convertir a ms
}

bool au915_verificar_duty_cycle(uint8_t canal, uint32_t tiempo_tx_ms) {
    static uint32_t tiempo_acumulado[AU915_CHANNELS_TOTAL] = {0};
    static uint32_t ventana_inicio = 0;
    
    uint32_t tiempo_actual = esp_timer_get_time() / 1000; // ms
    
    // Resetear ventana cada 100 segundos (duty cycle check)
    if (tiempo_actual - ventana_inicio >= 100000) {
        memset(tiempo_acumulado, 0, sizeof(tiempo_acumulado));
        ventana_inicio = tiempo_actual;
    }
    
    if (canal >= AU915_CHANNELS_TOTAL) {
        return false;
    }
    
    // Verificar si añadir este tiempo superaría el duty cycle
    uint32_t nuevo_tiempo = tiempo_acumulado[canal] + tiempo_tx_ms;
    uint32_t limite_ms = 100000 * AU915_DUTY_CYCLE_MAX / 100; // 10% de 100s
    
    if (nuevo_tiempo > limite_ms) {
        ESP_LOGW(TAG, "Canal %d excedería duty cycle: %lu/%lu ms", 
                 canal, nuevo_tiempo, limite_ms);
        return false;
    }
    
    // Registrar el tiempo de transmisión
    tiempo_acumulado[canal] = nuevo_tiempo;
    return true;
}

bool au915_verificar_potencia(int8_t potencia_dbm) {
    return (potencia_dbm >= AU915_POWER_MIN_DBM && 
            potencia_dbm <= AU915_POWER_MAX_DBM);
}

bool au915_verificar_dwell_time(uint32_t tiempo_ms) {
    return tiempo_ms <= AU915_DWELL_TIME_MS;
}

void fhss_init(fhss_config_t* config, uint32_t semilla) {
    if (!config) return;
    
    config->semilla = semilla;
    config->indice_actual = 0;
    config->ultimo_salto_ms = esp_timer_get_time() / 1000;
    
    // Generar secuencia pseudoaleatoria de canales válidos
    uint8_t canales_validos[AU915_CHANNELS_TOTAL];
    uint8_t num_validos = 0;
    
    for (uint8_t i = 0; i < AU915_CHANNELS_TOTAL; i++) {
        if (au915_canal_valido(i)) {
            canales_validos[num_validos++] = i;
        }
    }
    
    // Mezclar usando Linear Congruential Generator
    for (uint8_t i = 0; i < AU915_CHANNELS_TOTAL; i++) {
        if (i < num_validos) {
            uint32_t indice = (semilla * 1103515245 + 12345) % num_validos;
            config->secuencia[i] = canales_validos[indice];
            semilla = indice;
        } else {
            config->secuencia[i] = 0; // Canal por defecto
        }
    }
    
    ESP_LOGI(TAG, "FHSS inicializado con %d canales válidos", num_validos);
}

uint8_t fhss_siguiente_canal(fhss_config_t* config) {
    if (!config) return 0;
    
    config->indice_actual = (config->indice_actual + 1) % AU915_CHANNELS_TOTAL;
    
    // Buscar próximo canal válido
    uint8_t intentos = 0;
    while (!au915_canal_valido(config->secuencia[config->indice_actual]) && 
           intentos < AU915_CHANNELS_TOTAL) {
        config->indice_actual = (config->indice_actual + 1) % AU915_CHANNELS_TOTAL;
        intentos++;
    }
    
    config->ultimo_salto_ms = esp_timer_get_time() / 1000;
    
    return config->secuencia[config->indice_actual];
}

bool fhss_puede_transmitir(fhss_config_t* config, uint8_t canal) {
    if (!config) return false;
    
    // Verificar tiempo mínimo entre saltos
    uint32_t tiempo_actual = esp_timer_get_time() / 1000;
    if (tiempo_actual - config->ultimo_salto_ms < 50) { // Mínimo 50ms
        return false;
    }
    
    // Verificar que el canal está permitido
    if (!au915_canal_valido(canal)) {
        return false;
    }
    
    // Verificar dwell time
    return au915_verificar_dwell_time(100); // Máximo 100ms por transmisión
}
