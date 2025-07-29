# Arquitectura del Sistema de Alarma Vehicular

## Resumen Ejecutivo

Sistema de seguridad vehicular distribuido basado en ESP32-S3 con comunicación LoRa cifrada y autenticación multicapa. Implementa protocolos de seguridad de grado automotriz con cumplimiento normativo SUBTEL Chile.

## Componentes del Sistema

### 1. Llave Electrónica (Remoto)
- **Hardware**: ESP32-S3 + TFT ILI9341 + LoRa SX1276
- **Funciones**: Control remoto, interfaz usuario, gestión energía
- **Autonomía**: 30+ días con batería Li-ion 500mAh

### 2. Módulo Vehicular (Auto)
- **Hardware**: ESP32-S3 + LoRa SX1276 + Relés + Sensores
- **Funciones**: Control actuadores, monitoreo, seguridad
- **Alimentación**: 12V vehicular con respaldo de batería

## Arquitectura de Comunicaciones

```
┌─────────────┐    BLE LESC     ┌─────────────┐   LoRa P2P    ┌─────────────┐
│ Smartphone  │ <──────────────>│    Llave    │<─────────────>│    Auto     │
│    App      │   Proximidad    │  Electrónica│   Cifrado     │   Módulo    │
└─────────────┘    RSSI>-65     └─────────────┘   AES+HMAC    └─────────────┘
                                       │                              │
                                       │ ESP-NOW                     │
                                       ▼ Telemetría                  │
                                 ┌─────────────┐                     │
                                 │  Gateway/   │                     │
                                 │  Monitor    │<────────────────────┘
                                 └─────────────┘
```

## Protocolo de Seguridad

### Capas de Protección

1. **Capa Física**
   - Secure Boot v2 (RSA-3072)
   - Flash Encryption (AES-256-XTS)
   - eFuses write-protected

2. **Capa Criptográfica**
   - AES-128-CTR para cifrado de datos
   - HMAC-SHA256 para autenticación
   - Rolling codes anti-replay

3. **Capa de Protocolo**
   - Ventana temporal ≤1s
   - Contador monotónico
   - Challenge-response BLE

4. **Capa de Aplicación**
   - Estados de seguridad validados
   - Logs de auditoría
   - Detección de anomalías

### Estructura del Mensaje Seguro

```c
typedef struct __attribute__((packed)) {
    uint32_t magic;           // 0xAB12CD34
    uint8_t  version;         // Versión protocolo
    uint8_t  tipo_mensaje;    // Comando/respuesta/estado
    uint16_t longitud;        // Tamaño total
    uint32_t contador;        // Anti-replay counter
    
    // Payload cifrado AES-128-CTR
    struct {
        uint32_t timestamp;   // UNIX timestamp ms
        uint8_t  comando;     // CMD_UNLOCK, etc.
        uint8_t  datos[48];   // Parámetros comando
        uint32_t crc32;       // Integridad payload
    } payload_cifrado;
    
    uint8_t hmac[32];        // HMAC-SHA256 todo el mensaje
} mensaje_seguro_t;
```

## Flujos de Operación

### Flujo de Desbloqueo

1. **Iniciación**
   - Usuario presiona botón en llave
   - Llave verifica estado interno
   - Genera comando con rolling code

2. **Transmisión**
   - Cifrado AES-CTR del payload
   - Cálculo HMAC del mensaje completo
   - Transmisión LoRa con FHSS

3. **Recepción y Validación**
   - Auto recibe y valida HMAC
   - Verifica ventana temporal (≤1s)
   - Descifra y procesa comando

4. **Ejecución**
   - Verifica estado de sensores
   - Activa relé de puertas
   - Envía confirmación cifrada

### Flujo de Emergencia (Pánico)

1. **Activación**: Botón pánico o app móvil
2. **Transmisión**: Comando de máxima prioridad
3. **Respuesta**: Activación inmediata sirena/luces
4. **Notificación**: ESP-NOW a dispositivos cercanos

## Configuración de Canales AU915

### Compliance SUBTEL Chile

| Rango Frecuencia | Canales | SF | Potencia Max | Duty Cycle |
|------------------|---------|----| -------------|------------|
| 915.2-918.2 MHz  | 0-15    | 7-8| 20 dBm       | 10%        |
| 918.4-921.4 MHz  | 16-31   | 9-10| 17 dBm      | 10%        |
| 921.6-924.6 MHz  | 32-47   | 11-12| 14 dBm     | 10%        |
| 926.4-927.8 MHz  | 56-63   | 12  | 8 dBm       | 1%         |

### FHSS (Frequency Hopping)

- **Secuencia**: Pseudoaleatoria basada en semilla compartida
- **Dwell Time**: ≤400ms por canal (compliance)
- **Hop Rate**: Mínimo 50ms entre saltos
- **Adaptive**: Evita canales con interferencia

## Gestión de Energía

### Llave Electrónica

| Modo | Consumo | Duración | Descripción |
|------|---------|----------|-------------|
| Active | 80-150mA | Variable | Pantalla ON, transmisión |
| Idle | 15-25mA | Horas | BLE advertising, recepción |
| Light Sleep | 1-5mA | Minutos | CPU paused, RAM activa |
| Deep Sleep | 10-50μA | Días | Solo RTC, wake por botón/timer |

### Predictores de Consumo

- **Patrones de uso**: Histórico por hora del día
- **Predicción**: Algoritmo heurístico simple
- **Adaptación**: Timeouts dinámicos según batería

## Seguridad Avanzada

### Anti-Tampering

1. **Hardware**
   - Brown-out detection
   - Temperature monitoring
   - Supply voltage monitoring

2. **Software**
   - Code integrity checks
   - Memory protection
   - Execution flow validation

3. **Respuesta Gradual**
   - Nivel 1: Log del evento
   - Nivel 2: Deshabilitar funciones
   - Nivel 3: Borrar claves, modo seguro

### Mitigaciones de Ataques

| Tipo Ataque | Mitigación | Implementación |
|-------------|------------|----------------|
| Replay | Rolling code + timestamp | Ventana 1s, contador monotónico |
| Relay | RSSI validation + timing | BLE proximity, RTT <1ms |
| Jamming | FHSS adaptativo | 64 canales, detección ruido |
| Glitching | Brown-out + watchdog | Hardware + software monitors |
| Side-channel | eFuse keys + HMAC hw | Claves nunca en RAM |

## Diagnóstico y Mantenimiento

### Métricas Recolectadas

- **Comunicación**: RSSI, packet loss, latencia
- **Energía**: Voltaje batería, consumo por modo
- **Seguridad**: Intentos fallidos, anomalías
- **Sistema**: Uptime, reinicios, temperatura

### Caja Negra Forense

- **Capacidad**: 2048 eventos en memoria no volátil
- **Datos**: Timestamp, tipo evento, contexto
- **Acceso**: Solo mediante herramientas especiales
- **Integridad**: Checksums y firma digital

## Cumplimiento Normativo

### Estándares Aplicables

- **SUBTEL Chile**: Resolución Exenta N° 1776 (AU915)
- **FCC Part 15**: Compatibilidad electromagnética
- **ISO 26262**: Seguridad funcional automotriz
- **IEC 62443**: Ciberseguridad industrial

### Certificaciones Requeridas

1. **Homologación LoRa**: SUBTEL para 915-928 MHz
2. **EMC**: Emisiones/inmunidad electromagnética
3. **Automotriz**: ISO 16750 (temperatura, vibración)
4. **Ciberseguridad**: Evaluación penetration testing

## Roadmap Tecnológico

### Versión 1.0 (Actual)
- Comunicación LoRa básica
- Seguridad AES+HMAC
- Interfaz TFT simple

### Versión 1.5 (6 meses)
- OTA seguro
- Mesh networking ESP-NOW
- App móvil Android/iOS

### Versión 2.0 (12 meses)
- Conectividad celular opcional
- Machine learning anomalías
- Integration ecosistema IoT
