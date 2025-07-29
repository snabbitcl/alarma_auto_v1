#include "protocolo_seguro.h"
#include "cripto_utils.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char* TAG = "ROLLING_CODE";
static const char* NVS_NAMESPACE = "rolling_code";
static const char* NVS_KEY_COUNTER = "counter";

// Estado del rolling code
static struct {
    uint32_t contador_tx;
    uint32_t contador_rx_base;
    uint32_t ultimo_timestamp;
    bool inicializado;
} rolling_state = {0};

esp_err_t rolling_code_init(void) {
    if (rolling_state.inicializado) {
        return ESP_OK;
    }
    
    // Inicializar NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Abrir handle NVS
    nvs_handle_t nvs_handle;
    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error abriendo NVS: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Leer contador persistente
    size_t required_size = sizeof(rolling_state.contador_tx);
    ret = nvs_get_blob(nvs_handle, NVS_KEY_COUNTER, &rolling_state.contador_tx, &required_size);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        // Primera vez - inicializar con valor aleatorio
        esp_fill_random(&rolling_state.contador_tx, sizeof(rolling_state.contador_tx));
        rolling_state.contador_tx &= 0x7FFFFFFF; // Asegurar positivo
        
        ret = nvs_set_blob(nvs_handle, NVS_KEY_COUNTER, &rolling_state.contador_tx, sizeof(rolling_state.contador_tx));
        if (ret == ESP_OK) {
            ret = nvs_commit(nvs_handle);
        }
        
        ESP_LOGI(TAG, "Contador rolling code inicializado: %lu", rolling_state.contador_tx);
    } else if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Contador rolling code cargado: %lu", rolling_state.contador_tx);
    }
    
    nvs_close(nvs_handle);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error inicializando contador: %s", esp_err_to_name(ret));
        return ret;
    }
    
    rolling_state.contador_rx_base = 0;
    rolling_state.ultimo_timestamp = 0;
    rolling_state.inicializado = true;
    
    return ESP_OK;
}

esp_err_t rolling_code_generar_comando(uint32_t* contador_out, uint8_t* codigo_out) {
    if (!rolling_state.inicializado) {
        esp_err_t ret = rolling_code_init();
        if (ret != ESP_OK) {
            return ret;
        }
    }
    
    // Incrementar contador monotónico
    rolling_state.contador_tx++;
    *contador_out = rolling_state.contador_tx;
    
    // Obtener timestamp actual
    uint32_t timestamp = timestamp_actual_ms();
    rolling_state.ultimo_timestamp = timestamp;
    
    // Generar código HMAC
    esp_err_t ret = rolling_code_generar(rolling_state.contador_tx, timestamp, codigo_out);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error generando rolling code: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Guardar contador en NVS cada 10 incrementos
    if (rolling_state.contador_tx % 10 == 0) {
        nvs_handle_t nvs_handle;
        ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
        if (ret == ESP_OK) {
            nvs_set_blob(nvs_handle, NVS_KEY_COUNTER, &rolling_state.contador_tx, sizeof(rolling_state.contador_tx));
            nvs_commit(nvs_handle);
            nvs_close(nvs_handle);
        }
    }
    
    ESP_LOGD(TAG, "Rolling code generado - Contador: %lu, Timestamp: %lu", 
             rolling_state.contador_tx, timestamp);
    
    return ESP_OK;
}

esp_err_t rolling_code_validar_comando(uint32_t contador_rx, uint32_t timestamp_rx, const uint8_t* codigo_rx) {
    if (!rolling_state.inicializado) {
        esp_err_t ret = rolling_code_init();
        if (ret != ESP_OK) {
            return ret;
        }
    }
    
    // Verificar ventana temporal (1 segundo)
    uint32_t tiempo_actual = timestamp_actual_ms();
    uint32_t diferencia_tiempo = (tiempo_actual >= timestamp_rx) ? 
                                (tiempo_actual - timestamp_rx) : 
                                (timestamp_rx - tiempo_actual);
    
    if (diferencia_tiempo > 1000) {
        ESP_LOGW(TAG, "Mensaje fuera de ventana temporal: %lu ms", diferencia_tiempo);
        return ESP_ERR_TIMEOUT;
    }
    
    // Verificar que el contador es mayor que el último recibido
    if (contador_rx <= rolling_state.contador_rx_base) {
        ESP_LOGW(TAG, "Contador replay detectado: %lu <= %lu", 
                 contador_rx, rolling_state.contador_rx_base);
        return ESP_ERR_INVALID_STATE;
    }
    
    // Verificar que no está muy adelante (ventana de 1000 comandos)
    if (contador_rx > rolling_state.contador_rx_base + 1000) {
        ESP_LOGW(TAG, "Contador muy adelantado: %lu > %lu", 
                 contador_rx, rolling_state.contador_rx_base + 1000);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Validar el código HMAC
    esp_err_t ret = rolling_code_validar(contador_rx, timestamp_rx, codigo_rx);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Rolling code inválido");
        return ret;
    }
    
    // Actualizar base del contador
    rolling_state.contador_rx_base = contador_rx;
    rolling_state.ultimo_timestamp = timestamp_rx;
    
    ESP_LOGI(TAG, "Rolling code validado - Contador: %lu, Timestamp: %lu", 
             contador_rx, timestamp_rx);
    
    return ESP_OK;
}

uint32_t rolling_code_obtener_contador_actual(void) {
    return rolling_state.contador_tx;
}

uint32_t rolling_code_obtener_contador_base_rx(void) {
    return rolling_state.contador_rx_base;
}

esp_err_t rolling_code_sincronizar(uint32_t nuevo_contador_base) {
    if (!rolling_state.inicializado) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Solo permitir avanzar el contador, nunca retroceder
    if (nuevo_contador_base > rolling_state.contador_rx_base) {
        rolling_state.contador_rx_base = nuevo_contador_base;
        ESP_LOGI(TAG, "Contador base sincronizado a: %lu", nuevo_contador_base);
        return ESP_OK;
    }
    
    return ESP_ERR_INVALID_ARG;
}

esp_err_t rolling_code_reset_contadores(void) {
    // Solo para desarrollo - NO usar en producción
    #ifdef CONFIG_DESARROLLO
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret == ESP_OK) {
        nvs_erase_key(nvs_handle, NVS_KEY_COUNTER);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        
        rolling_state.contador_tx = 0;
        rolling_state.contador_rx_base = 0;
        
        ESP_LOGW(TAG, "CONTADORES RESETEADOS - SOLO DESARROLLO");
        return ESP_OK;
    }
    return ret;
    #else
    ESP_LOGE(TAG, "Reset contadores no permitido en producción");
    return ESP_ERR_NOT_SUPPORTED;
    #endif
}
