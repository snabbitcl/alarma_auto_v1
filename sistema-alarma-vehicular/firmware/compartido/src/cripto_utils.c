#include "cripto_utils.h"
#include "protocolo_seguro.h"  // Para ROLLING_CODE_SIZE
#include "esp_efuse.h"
#include "esp_hmac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "mbedtls/aes.h"
#include "mbedtls/sha256.h"
#include "esp_log.h"
#include "string.h"

static const char* TAG = "CRIPTO";

// Contexto global del subsistema criptográfico
static contexto_cripto_t ctx_cripto = {0};

esp_err_t cripto_init(void) {
    if (ctx_cripto.claves_cargadas) {
        return ESP_OK;
    }
    
    // Verificar integridad del hardware criptográfico
    esp_err_t ret = verificar_integridad_cripto();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Verificación integridad cripto falló: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Subsistema criptográfico inicializado");
    return ESP_OK;
}

esp_err_t cripto_cargar_claves_efuse(void) {
    // Verificar que los bloques de claves están grabados
    if (!esp_efuse_key_block_unused(EFUSE_BLK_KEY3) && 
        !esp_efuse_key_block_unused(EFUSE_BLK_KEY4)) {
        
        ESP_LOGI(TAG, "Claves cargadas desde eFuse");
        ctx_cripto.claves_cargadas = true;
        ctx_cripto.contador_uso = 0;
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Claves eFuse no encontradas");
        return ESP_ERR_NOT_FOUND;
    }
}

esp_err_t aes_ctr_cifrar(const uint8_t* plaintext, size_t len, 
                        const uint8_t* nonce, uint8_t* ciphertext) {
    if (!ctx_cripto.claves_cargadas) {
        return ESP_ERR_INVALID_STATE;
    }
    
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    
    // Usar clave desde eFuse BLK3
    uint8_t clave_aes[AES_128_KEY_SIZE];
    esp_err_t ret = esp_efuse_read_block(EFUSE_BLK_KEY3, clave_aes, 0, AES_128_KEY_SIZE * 8);
    if (ret != ESP_OK) {
        mbedtls_aes_free(&aes);
        return ret;
    }
    
    int mbedtls_ret = mbedtls_aes_setkey_enc(&aes, clave_aes, 128);
    if (mbedtls_ret != 0) {
        limpiar_memoria_segura(clave_aes, AES_128_KEY_SIZE);
        mbedtls_aes_free(&aes);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Preparar counter block
    uint8_t counter_block[16] = {0};
    memcpy(counter_block, nonce, NONCE_SIZE);
    
    size_t nc_off = 0;
    uint8_t stream_block[16];
    
    mbedtls_ret = mbedtls_aes_crypt_ctr(&aes, len, &nc_off, counter_block, 
                                       stream_block, plaintext, ciphertext);
    
    // Limpiar secretos
    limpiar_memoria_segura(clave_aes, AES_128_KEY_SIZE);
    limpiar_memoria_segura(counter_block, 16);
    limpiar_memoria_segura(stream_block, 16);
    mbedtls_aes_free(&aes);
    
    ctx_cripto.contador_uso++;
    
    return (mbedtls_ret == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t aes_ctr_descifrar(const uint8_t* ciphertext, size_t len,
                           const uint8_t* nonce, uint8_t* plaintext) {
    // AES-CTR es simétrico - misma función para cifrar y descifrar
    return aes_ctr_cifrar(ciphertext, len, nonce, plaintext);
}

esp_err_t hmac_calcular(const uint8_t* data, size_t len, uint8_t* hmac_out) {
    if (!ctx_cripto.claves_cargadas) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Usar periférico HMAC con clave en eFuse BLK4
    esp_err_t ret = esp_hmac_calculate(HMAC_KEY_ID_4, data, len, hmac_out);
    if (ret == ESP_OK) {
        ctx_cripto.contador_uso++;
    }
    
    return ret;
}

esp_err_t hmac_verificar(const uint8_t* data, size_t len, const uint8_t* hmac_esperado) {
    uint8_t hmac_calculado[HMAC_SHA256_SIZE];
    
    esp_err_t ret = hmac_calcular(data, len, hmac_calculado);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Comparación en tiempo constante
    int resultado = 0;
    for (int i = 0; i < HMAC_SHA256_SIZE; i++) {
        resultado |= hmac_calculado[i] ^ hmac_esperado[i];
    }
    
    limpiar_memoria_segura(hmac_calculado, HMAC_SHA256_SIZE);
    
    return (resultado == 0) ? ESP_OK : ESP_ERR_INVALID_MAC;
}

esp_err_t rolling_code_generar(uint32_t contador, uint32_t timestamp, uint8_t* codigo_out) {
    // Estructura de datos para el rolling code
    struct {
        uint32_t contador;
        uint32_t timestamp;
        uint8_t salt[8];
    } datos_rolling;
    
    datos_rolling.contador = contador;
    datos_rolling.timestamp = timestamp;
    
    // Añadir salt aleatorio
    esp_err_t ret = random_bytes(datos_rolling.salt, sizeof(datos_rolling.salt));
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Calcular HMAC del conjunto
    ret = hmac_calcular((uint8_t*)&datos_rolling, sizeof(datos_rolling), codigo_out);
    
    // Limpiar datos sensibles
    limpiar_memoria_segura(&datos_rolling, sizeof(datos_rolling));
    
    return ret;
}

esp_err_t rolling_code_validar(uint32_t contador, uint32_t timestamp, const uint8_t* codigo) {
    uint8_t codigo_calculado[ROLLING_CODE_SIZE];
    
    esp_err_t ret = rolling_code_generar(contador, timestamp, codigo_calculado);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Verificar con tolerancia temporal
    ret = hmac_verificar((uint8_t*)&timestamp, sizeof(timestamp) + sizeof(contador), codigo);
    
    limpiar_memoria_segura(codigo_calculado, ROLLING_CODE_SIZE);
    
    return ret;
}

esp_err_t sha256_calcular(const uint8_t* data, size_t len, uint8_t* hash_out) {
    mbedtls_sha256_context sha256;
    mbedtls_sha256_init(&sha256);
    
    int ret = mbedtls_sha256_starts(&sha256, 0); // 0 = SHA256, no SHA224
    if (ret != 0) {
        mbedtls_sha256_free(&sha256);
        return ESP_FAIL;
    }
    
    ret = mbedtls_sha256_update(&sha256, data, len);
    if (ret != 0) {
        mbedtls_sha256_free(&sha256);
        return ESP_FAIL;
    }
    
    ret = mbedtls_sha256_finish(&sha256, hash_out);
    mbedtls_sha256_free(&sha256);
    
    return (ret == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t random_bytes(uint8_t* buffer, size_t len) {
    if (!buffer || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_fill_random(buffer, len);
    return ESP_OK;
}

bool validar_ventana_temporal(uint32_t timestamp, uint32_t ventana_ms) {
    uint32_t tiempo_actual = timestamp_actual_ms();
    uint32_t diferencia;
    
    if (tiempo_actual >= timestamp) {
        diferencia = tiempo_actual - timestamp;
    } else {
        // Posible wrap-around
        diferencia = timestamp - tiempo_actual;
    }
    
    return diferencia <= ventana_ms;
}

uint32_t timestamp_actual_ms(void) {
    return esp_timer_get_time() / 1000;
}

void limpiar_memoria_segura(void* ptr, size_t len) {
    if (ptr && len > 0) {
        volatile uint8_t* p = (volatile uint8_t*)ptr;
        for (size_t i = 0; i < len; i++) {
            p[i] = 0;
        }
    }
}

esp_err_t verificar_integridad_cripto(void) {
    // Verificar que los periféricos criptográficos están disponibles
    
    // Test básico HMAC
    uint8_t test_data[] = "test";
    uint8_t hmac_test[32];
    
    if (!esp_efuse_key_block_unused(EFUSE_BLK_KEY4)) {
        esp_err_t ret = esp_hmac_calculate(HMAC_KEY_ID_4, test_data, 4, hmac_test);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Test HMAC falló: %s", esp_err_to_name(ret));
            return ret;
        }
    }
    
    // Test básico AES (con clave temporal)
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    
    uint8_t clave_test[16] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
                             0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};
    
    int ret = mbedtls_aes_setkey_enc(&aes, clave_test, 128);
    mbedtls_aes_free(&aes);
    limpiar_memoria_segura(clave_test, 16);
    
    if (ret != 0) {
        ESP_LOGE(TAG, "Test AES falló: %d", ret);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Verificación integridad cripto exitosa");
    return ESP_OK;
}
