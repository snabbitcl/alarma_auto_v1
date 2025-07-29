/**
 * @file gpio_config.h
 * @brief Configuración centralizada de pines GPIO para módulo vehicular
 * 
 * Definiciones de todos los pines GPIO utilizados en el sistema de alarma
 * vehicular, organizados por funcionalidad
 * 
 * @author Sistema Alarma Vehicular
 * @date 2024
 */

#ifndef GPIO_CONFIG_H
#define GPIO_CONFIG_H

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// CONFIGURACIÓN DE PINES SENSORES DIGITALES
// ============================================================================

// Sensores de puertas (reed switches normalmente cerrados)
#define CONFIG_GPIO_PUERTA_CONDUCTOR        GPIO_NUM_10
#define CONFIG_GPIO_PUERTA_PASAJERO         GPIO_NUM_11
#define CONFIG_GPIO_PUERTA_TRASERA_IZQ      GPIO_NUM_12
#define CONFIG_GPIO_PUERTA_TRASERA_DER      GPIO_NUM_13

// Sensores de apertura
#define CONFIG_GPIO_CAPO                    GPIO_NUM_14
#define CONFIG_GPIO_BAUL                    GPIO_NUM_15

// Sensores de movimiento y vibración
#define CONFIG_GPIO_PIR                     GPIO_NUM_16
#define CONFIG_GPIO_SHOCK                   GPIO_NUM_17

// Sensor tamper (normalmente cerrado)
#define CONFIG_GPIO_TAMPER                  GPIO_NUM_18

// ============================================================================
// CONFIGURACIÓN DE PINES ACTUADORES
// ============================================================================

// Relés principales (activo alto con optoacopladores)
#define CONFIG_GPIO_RELE_SIRENA             GPIO_NUM_21
#define CONFIG_GPIO_RELE_LUCES              GPIO_NUM_47
#define CONFIG_GPIO_RELE_BLOQUEO            GPIO_NUM_48
#define CONFIG_GPIO_RELE_GPS                GPIO_NUM_45

// LEDs de estado
#define CONFIG_GPIO_LED_ESTADO              GPIO_NUM_8
#define CONFIG_GPIO_LED_ALARMA              GPIO_NUM_9

// Buzzer local
#define CONFIG_GPIO_BUZZER                  GPIO_NUM_19

// ============================================================================
// CONFIGURACIÓN ADC PARA SENSORES ANALÓGICOS
// ============================================================================

// Canales ADC1 - corregido para coincidencia con GPIO
#define CONFIG_ADC_CANAL_BATERIA            ADC_CHANNEL_0    // GPIO36
#define CONFIG_ADC_CANAL_CORRIENTE          ADC_CHANNEL_3    // GPIO39  
#define CONFIG_ADC_CANAL_TEMPERATURA        ADC_CHANNEL_6    // GPIO34

// Pines GPIO alternativos para ADC (evitar conflictos con relés)
#define CONFIG_GPIO_ADC_BATERIA             GPIO_NUM_36  // ADC1_CHANNEL_0 - sin conflictos
#define CONFIG_GPIO_ADC_CORRIENTE           GPIO_NUM_39  // ADC1_CHANNEL_3 - sin conflictos  
#define CONFIG_GPIO_ADC_TEMPERATURA         GPIO_NUM_34  // ADC1_CHANNEL_6 - sin conflictos

// ============================================================================
// CONFIGURACIÓN SPI PARA COMUNICACIÓN LORA
// ============================================================================

// SPI2 para SX1276 LoRa - corregido conflictos
#define CONFIG_GPIO_SPI2_MISO               GPIO_NUM_37  
#define CONFIG_GPIO_SPI2_MOSI               GPIO_NUM_35
#define CONFIG_GPIO_SPI2_CLK                GPIO_NUM_38  // Cambiado de GPIO_NUM_36 (conflicto con ADC)
#define CONFIG_GPIO_LORA_CS                 GPIO_NUM_33  // Cambiado de GPIO_NUM_34 (conflicto con ADC)
#define CONFIG_GPIO_LORA_RST                GPIO_NUM_32  // Cambiado de GPIO_NUM_33 (conflicto)
#define CONFIG_GPIO_LORA_IRQ                GPIO_NUM_26  // Cambiado de GPIO_NUM_38 (conflicto con CLK)

// ============================================================================
// CONFIGURACIÓN UART PARA DEBUGGING
// ============================================================================

// UART0 - Debug console (pins fijos)
#define CONFIG_GPIO_UART0_TX                GPIO_NUM_43
#define CONFIG_GPIO_UART0_RX                GPIO_NUM_44

// ============================================================================
// CONFIGURACIÓN DE CARACTERÍSTICAS ELÉCTRICAS  
// ============================================================================

// Configuración de pull-ups/pull-downs
#define SENSORES_PULL_UP_ENABLE             true
#define SENSORES_PULL_DOWN_ENABLE           false

// Configuración de interrupciones
#define SENSORES_INTERRUPT_TYPE             GPIO_INTR_ANYEDGE
#define TAMPER_INTERRUPT_TYPE               GPIO_INTR_NEGEDGE  // Solo flanco negativo

// Timeouts y delays
#define GPIO_DEBOUNCE_TIME_MS               100
#define ACTUADOR_SAFETY_TIMEOUT_MS          300000  // 5 minutos

// Configuración ADC
#define ADC_RESOLUTION                      ADC_BITWIDTH_12
#define ADC_ATTENUATION                     ADC_ATTEN_DB_12  // 0-3.3V (nueva API)
#define ADC_SAMPLES_FOR_AVERAGE             8

#ifdef __cplusplus
}
#endif

#endif // GPIO_CONFIG_H
