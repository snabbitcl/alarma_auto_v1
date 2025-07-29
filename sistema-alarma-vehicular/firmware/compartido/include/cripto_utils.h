#ifndef CRIPTO_UTILS_H
#define CRIPTO_UTILS_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Tamaños de claves y hashes
#define AES_128_KEY_SIZE    16
#define HMAC_SHA256_SIZE    32
#define SHA256_SIZE         32
#define NONCE_SIZE          12
// ROLLING_CODE_SIZE se define en protocolo_seguro.h

// IDs de bloques eFuse
#define EFUSE_BLK_AES       3   // Bloque para clave AES
#define EFUSE_BLK_HMAC      4   // Bloque para clave HMAC

// IDs de claves HMAC hardware
#define HMAC_KEY_ID_4       4   // ID de clave HMAC en bloque 4

// Estructura para contexto criptográfico
typedef struct {
    uint8_t clave_aes[AES_128_KEY_SIZE];
    uint8_t clave_hmac[HMAC_SHA256_SIZE];
    bool claves_cargadas;
    uint32_t contador_uso;
} contexto_cripto_t;

// Inicialización del subsistema criptográfico
esp_err_t cripto_init(void);
esp_err_t cripto_cargar_claves_efuse(void);

// Funciones AES-CTR
esp_err_t aes_ctr_cifrar(const uint8_t* plaintext, size_t len, 
                        const uint8_t* nonce, uint8_t* ciphertext);
esp_err_t aes_ctr_descifrar(const uint8_t* ciphertext, size_t len,
                           const uint8_t* nonce, uint8_t* plaintext);

// Funciones HMAC
esp_err_t hmac_calcular(const uint8_t* data, size_t len, uint8_t* hmac_out);
esp_err_t hmac_verificar(const uint8_t* data, size_t len, const uint8_t* hmac_esperado);

// Rolling code
esp_err_t rolling_code_generar(uint32_t contador, uint32_t timestamp, uint8_t* codigo_out);
esp_err_t rolling_code_validar(uint32_t contador, uint32_t timestamp, const uint8_t* codigo);

// Hash SHA256
esp_err_t sha256_calcular(const uint8_t* data, size_t len, uint8_t* hash_out);

// Generación segura de números aleatorios
esp_err_t random_bytes(uint8_t* buffer, size_t len);

// Funciones de validación temporal
bool validar_ventana_temporal(uint32_t timestamp, uint32_t ventana_ms);
uint32_t timestamp_actual_ms(void);

// Limpieza segura de memoria
void limpiar_memoria_segura(void* ptr, size_t len);

// Verificación de integridad del hardware
esp_err_t verificar_integridad_cripto(void);

#ifdef __cplusplus
}
#endif

#endif // CRIPTO_UTILS_H
