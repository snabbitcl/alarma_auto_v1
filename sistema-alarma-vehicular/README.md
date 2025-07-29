# Sistema de Alarma Automotriz ESP32-S3

Sistema de seguridad vehicular de grado automotriz con llave electrónica LoRa.

## 🏗️ Arquitectura

- **Llave**: Control remoto con pantalla TFT y comunicación LoRa/BLE
- **Auto**: Módulo vehicular con control de actuadores y sensores
- **Seguridad**: Secure Boot v2, Flash Encryption, Rolling Code HMAC

## 📡 Especificaciones Técnicas

### Hardware
- **MCU**: ESP32-S3-WROOM-1 (N16R8) - 240MHz dual-core, 16MB Flash, 8MB PSRAM
- **Radio LoRa**: SX1276 SPI - 915-928 MHz AU915, +20dBm
- **Pantalla** (llave): ILI9341 2.4" 320x240 SPI
- **Actuadores** (auto): 4 relés opto-aislados + buzzer
- **Sensores** (auto): 4 digitales + ADC batería

### Alcances
- **Distancia LoRa**: 2-5 km urbano, 10+ km rural
- **Latencia comando**: <300ms end-to-end
- **Autonomía**: 30+ días (llave), indefinida (auto)
- **Temperatura**: -20°C a +85°C automotriz

## 🚀 Inicio Rápido

```bash
# Clonar repositorio
git clone https://github.com/tu-usuario/sistema-alarma-vehicular
cd sistema-alarma-vehicular

# Compilar todo
./compilar.cmd

# Flashear dispositivos
herramientas\flasheo\flashear_llave.cmd COM3
herramientas\flasheo\flashear_auto.cmd COM4
```

## 📁 Estructura

- `firmware/llave/` - Firmware del control remoto
- `firmware/auto/` - Firmware del módulo vehicular  
- `firmware/compartido/` - Código común (protocolo, crypto)
- `documentacion/` - Manuales y especificaciones
- `herramientas/` - Scripts de desarrollo

## 🔐 Seguridad

- Cifrado AES-128-CTR + HMAC-SHA256
- Anti-replay con ventana temporal 1s
- Claves únicas por dispositivo en eFuses
- Secure Boot v2 + Flash Encryption

## 📡 Comunicaciones

- **LoRa**: 915-928 MHz (AU915 SUBTEL Chile)
- **BLE 5.0**: Secure Connections (ECDH-P256)
- **ESP-NOW**: Alarmas de corto alcance

## 🛠️ Desarrollo

### Requisitos
- ESP-IDF v5.5+
- Python 3.8+
- CMake 3.16+

### Compilación
```bash
# Configurar ESP-IDF
set IDF_PATH=C:\esp\esp-idf
call %IDF_PATH%\export.bat

# Compilar llave
cd firmware
idf.py -D PROYECTO_OBJETIVO=llave -B build_llave build

# Compilar auto
idf.py -D PROYECTO_OBJETIVO=auto -B build_auto build
```

### Testing
```bash
# Tests unitarios
cd pruebas\unitarias
python -m pytest

# Tests de integración
cd pruebas\integracion
python test_comunicacion_e2e.py
```

## 📄 Documentación

- [Arquitectura del Sistema](documentacion/ARQUITECTURA.md)
- [Protocolo de Seguridad](documentacion/SEGURIDAD.md)
- [Manual de Usuario](documentacion/MANUAL-USUARIO.md)

## 📄 Licencia

Proprietario - Todos los derechos reservados
