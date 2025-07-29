#include "protocolo_seguro.h"
#include "cripto_utils.h"
#include "config_au915.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_crc.h"
#include "string.h"

static const char* TAG = "PROTOCOLO";

// Estado global del protocolo
static struct {
    bool inicializado;
    uint32_t contador_tx;
    uint32_t contador_rx_base;
    uint32_t ventana_replay[4]; // Bitmap para 128 mensajes
    uint32_t ultimo_keepalive;
} protocolo_estado = {0};

esp_err_t protocolo_init(void) {
    if (protocolo_estado.inicializado) {
        return ESP_OK;
    }
    
    // Inicializar subsistema criptográfico
    esp_err_t ret = cripto_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error inicializando crypto: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Cargar claves desde eFuse
    ret = cripto_cargar_claves_efuse();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error cargando claves eFuse: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Inicializar contadores
    protocolo_estado.contador_tx = 1;
    protocolo_estado.contador_rx_base = 0;
    memset(protocolo_estado.ventana_replay, 0, sizeof(protocolo_estado.ventana_replay));
    protocolo_estado.ultimo_keepalive = esp_timer_get_time() / 1000;
    
    protocolo_estado.inicializado = true;
    ESP_LOGI(TAG, "Protocolo seguro inicializado");
    
    return ESP_OK;
}

esp_err_t protocolo_enviar_comando(comando_vehiculo_t cmd, uint8_t* datos, size_t len) {
    if (!protocolo_estado.inicializado) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (len > PAYLOAD_MAX_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }
    
    mensaje_seguro_t mensaje = {0};
    
    // Preparar header
    mensaje.magic = PROTOCOLO_MAGIC;
    mensaje.version = PROTOCOLO_VERSION;
    mensaje.tipo_mensaje = MSG_COMANDO;
    mensaje.longitud = sizeof(mensaje_seguro_t);
    mensaje.contador = protocolo_estado.contador_tx++;
    
    // Preparar payload
    mensaje.payload_cifrado.timestamp = timestamp_actual_ms();
    mensaje.payload_cifrado.comando = cmd;
    if (datos && len > 0) {
        memcpy(mensaje.payload_cifrado.datos, datos, len);
    }
    
    // Calcular CRC del payload antes del cifrado
    mensaje.payload_cifrado.crc32 = esp_crc32_le(0, 
        (uint8_t*)&mensaje.payload_cifrado, 
        sizeof(mensaje.payload_cifrado) - sizeof(uint32_t));
    
    // Cifrar payload con AES-CTR
    uint8_t nonce[NONCE_SIZE] = {0};
    memcpy(nonce, &mensaje.contador, sizeof(mensaje.contador));
    memcpy(nonce + 4, &mensaje.payload_cifrado.timestamp, sizeof(uint32_t));
    
    esp_err_t ret = aes_ctr_cifrar((uint8_t*)&mensaje.payload_cifrado,
                                   sizeof(mensaje.payload_cifrado),
                                   nonce,
                                   (uint8_t*)&mensaje.payload_cifrado);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error cifrando payload: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Calcular HMAC de todo el mensaje excepto el propio HMAC
    ret = hmac_calcular((uint8_t*)&mensaje, 
                       sizeof(mensaje_seguro_t) - HMAC_SIZE,
                       mensaje.hmac);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error calculando HMAC: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Comando %d enviado, contador: %lu", cmd, mensaje.contador);
    
    // TODO: Enviar por LoRa con FHSS
    // return lora_transmitir((uint8_t*)&mensaje, sizeof(mensaje));
    
    return ESP_OK;
}

esp_err_t protocolo_procesar_mensaje(uint8_t* buffer, size_t len, mensaje_seguro_t* msg_out) {
    if (!protocolo_estado.inicializado || !buffer || !msg_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (len < sizeof(mensaje_seguro_t)) {
        ESP_LOGW(TAG, "Mensaje muy corto: %d bytes", len);
        return ESP_ERR_INVALID_SIZE;
    }
    
    // Copiar mensaje recibido
    memcpy(msg_out, buffer, sizeof(mensaje_seguro_t));
    
    // Verificar magic number
    if (msg_out->magic != PROTOCOLO_MAGIC) {
        ESP_LOGW(TAG, "Magic number inválido: 0x%08lx", msg_out->magic);
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    // Verificar versión
    if (msg_out->version != PROTOCOLO_VERSION) {
        ESP_LOGW(TAG, "Versión no soportada: %d", msg_out->version);
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    // Verificar HMAC
    uint8_t hmac_calculado[HMAC_SIZE];
    esp_err_t ret = hmac_calcular((uint8_t*)msg_out,
                                 sizeof(mensaje_seguro_t) - HMAC_SIZE,
                                 hmac_calculado);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error calculando HMAC: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Comparación en tiempo constante
    if (memcmp(hmac_calculado, msg_out->hmac, HMAC_SIZE) != 0) {
        ESP_LOGW(TAG, "HMAC inválido");
        return ESP_ERR_INVALID_MAC;
    }
    
    // Verificar anti-replay
    if (!validar_contador_replay(msg_out->contador)) {
        ESP_LOGW(TAG, "Contador replay inválido: %lu", msg_out->contador);
        return ESP_ERR_INVALID_STATE;
    }
    
    // Descifrar payload
    uint8_t nonce[NONCE_SIZE] = {0};
    memcpy(nonce, &msg_out->contador, sizeof(msg_out->contador));
    // El timestamp va en el nonce después del descifrado
    
    ret = aes_ctr_descifrar((uint8_t*)&msg_out->payload_cifrado,
                           sizeof(msg_out->payload_cifrado),
                           nonce,
                           (uint8_t*)&msg_out->payload_cifrado);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error descifrando payload: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Verificar CRC del payload
    uint32_t crc_calculado = esp_crc32_le(0,
        (uint8_t*)&msg_out->payload_cifrado,
        sizeof(msg_out->payload_cifrado) - sizeof(uint32_t));
    
    if (crc_calculado != msg_out->payload_cifrado.crc32) {
        ESP_LOGW(TAG, "CRC payload inválido");
        return ESP_ERR_INVALID_CRC;
    }
    
    // Verificar ventana temporal
    if (!validar_ventana_temporal(msg_out->payload_cifrado.timestamp, 1000)) {
        ESP_LOGW(TAG, "Mensaje fuera de ventana temporal");
        return ESP_ERR_TIMEOUT;
    }
    
    // Actualizar ventana anti-replay
    actualizar_ventana_replay(msg_out->contador);
    
    ESP_LOGI(TAG, "Mensaje procesado exitosamente. Tipo: %d, Contador: %lu", 
             msg_out->tipo_mensaje, msg_out->contador);
    
    return ESP_OK;
}

bool validar_contador_replay(uint32_t contador) {
    // Si el contador es menor que la base, rechazar
    if (contador < protocolo_estado.contador_rx_base) {
        return false;
    }
    
    // Si está muy adelante, rechazar (evita overflow del bitmap)
    uint32_t diferencia = contador - protocolo_estado.contador_rx_base;
    if (diferencia >= 128) {
        return false;
    }
    
    // Verificar en el bitmap
    uint32_t byte_index = diferencia / 32;
    uint32_t bit_index = diferencia % 32;
    
    if (protocolo_estado.ventana_replay[byte_index] & (1UL << bit_index)) {
        // Ya recibido
        return false;
    }
    
    return true;
}

void actualizar_ventana_replay(uint32_t contador) {
    uint32_t diferencia = contador - protocolo_estado.contador_rx_base;
    
    if (diferencia >= 128) {
        // Mover ventana
        uint32_t shift = diferencia - 127;
        protocolo_estado.contador_rx_base += shift;
        
        // Shift del bitmap
        if (shift >= 128) {
            memset(protocolo_estado.ventana_replay, 0, sizeof(protocolo_estado.ventana_replay));
        } else {
            // TODO: Implementar shift complejo del bitmap
        }
        diferencia = 127;
    }
    
    // Marcar como recibido
    uint32_t byte_index = diferencia / 32;
    uint32_t bit_index = diferencia % 32;
    protocolo_estado.ventana_replay[byte_index] |= (1UL << bit_index);
}

uint32_t obtener_timestamp_seguro(void) {
    return timestamp_actual_ms();
}

uint32_t obtener_contador_monotono(void) {
    return protocolo_estado.contador_tx;
}
