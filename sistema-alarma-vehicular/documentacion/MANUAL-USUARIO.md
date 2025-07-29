# Manual de Usuario - Sistema de Alarma Vehicular

## Introducción

El Sistema de Alarma Vehicular ESP32-S3 es una solución completa de seguridad automotriz que proporciona control remoto cifrado y monitoreo avanzado para vehículos.

## Componentes del Kit

### Llave Electrónica
- Control remoto con pantalla TFT 2.4"
- Batería recargable Li-ion 500mAh
- Alcance: hasta 5km en condiciones óptimas
- Autonomía: 30+ días de uso normal

### Módulo Vehicular
- Unidad principal de control
- 4 relés para actuadores
- Sensores de puerta y movimiento
- Conexión a sistema eléctrico 12V

## Instalación

### 1. Preparación del Vehículo
1. Desconectar batería del vehículo
2. Localizar cables del sistema de bloqueo central
3. Identificar puntos de conexión para sensores

### 2. Instalación del Módulo Vehicular
```
Conexiones principales:
- Rojo (+12V): Batería permanente
- Negro (GND): Chasis/masa
- Azul: Control bloqueo central
- Verde: Corte de ignición
- Amarillo: Sirena/luces
- Blanco: Sensores auxiliares
```

### 3. Configuración Inicial
1. Encender llave electrónica
2. Presionar MENÚ > CONFIGURACIÓN
3. Seguir asistente de pareado
4. Probar funciones básicas

## Operación Básica

### Bloqueo del Vehículo
1. Presionar botón "BLOQUEAR" 
2. Esperar confirmación sonora (2 pitidos)
3. LED de estado parpadea 3 veces
4. Pantalla muestra "VEHÍCULO BLOQUEADO"

### Desbloqueo del Vehículo
1. Presionar botón "DESBLOQUEAR"
2. Esperar confirmación sonora (1 pitido)
3. LED de estado se enciende fijo
4. Pantalla muestra "VEHÍCULO LIBRE"

### Función Pánico
1. Mantener presionado botón "PÁNICO" por 2 segundos
2. Sirena se activa inmediatamente
3. Luces parpadean por 30 segundos
4. Para cancelar: presionar cualquier botón

### Buscar Vehículo
1. Presionar botón "BUSCAR"
2. Luces parpadean 10 veces
3. Bocina suena 3 veces cortas
4. Útil en estacionamientos grandes

## Funciones Avanzadas

### Configuración de Horarios
- MENÚ > CONFIGURACIÓN > HORARIOS
- Configurar auto-bloqueo nocturno
- Definir horarios de mayor seguridad

### Monitoreo de Batería
- Indicador en pantalla principal
- Alerta automática bajo 20%
- Modo ahorro bajo 10%

### Historial de Eventos
- MENÚ > HISTORIAL
- Últimos 50 eventos registrados
- Horarios de uso del vehículo

## Solución de Problemas

### Llave No Responde
1. Verificar carga de batería
2. Mantener presionado botón de encendido 5s
3. Si persiste, contactar soporte técnico

### Sin Comunicación con Vehículo
1. Verificar distancia (máximo 5km)
2. Revisar obstáculos metálicos
3. Probar desde ubicación diferente
4. Verificar conexiones del módulo vehicular

### Alarma Se Activa Sola
1. Revisar sensores de puerta
2. Verificar sensor de movimiento
3. Ajustar sensibilidad en configuración
4. Revisar instalación eléctrica

### Batería Se Agota Rápido
1. Reducir brillo de pantalla
2. Disminuir tiempo de pantalla activa
3. Verificar temperatura de operación
4. Revisar patrón de uso

## Mantenimiento

### Cuidado de la Llave
- Evitar golpes y caídas
- No exponer a temperaturas extremas (-20°C a +60°C)
- Cargar cuando indicador esté en rojo
- Limpiar pantalla con paño suave

### Cuidado del Módulo Vehicular
- Verificar conexiones cada 6 meses
- Mantener libre de humedad
- Revisar antena LoRa (no doblar)
- Limpiar contactos si hay corrosión

## Especificaciones Técnicas

### Llave Electrónica
- **Procesador**: ESP32-S3 dual-core 240MHz
- **Pantalla**: TFT 2.4" 320x240 pixels
- **Radio**: LoRa 915-928 MHz, hasta +20dBm
- **Batería**: Li-ion 500mAh, 3.7V
- **Dimensiones**: 120 x 60 x 15 mm
- **Peso**: 85 gramos

### Módulo Vehicular
- **Procesador**: ESP32-S3 dual-core 240MHz
- **Alimentación**: 12V DC (rango 9-16V)
- **Consumo**: 50mA en standby, 200mA activo
- **Relés**: 4x 10A/240VAC, opto-aislados
- **Temperatura**: -25°C a +85°C
- **Protección**: IP54 (a prueba de salpicaduras)

## Garantía y Soporte

### Cobertura de Garantía
- **Duración**: 2 años desde fecha de compra
- **Incluye**: Defectos de fabricación y materiales
- **Excluye**: Daños por mal uso, agua, golpes

### Contacto Soporte
- **Email**: soporte@alarma-vehicular.com
- **Teléfono**: +56-2-XXXX-XXXX
- **Horario**: Lunes a Viernes 9:00-18:00
- **Web**: https://alarma-vehicular.com/soporte

### Registro de Producto
Para activar la garantía, registre su producto en:
https://alarma-vehicular.com/registro

## Cumplimiento Normativo

- **SUBTEL Chile**: Certificado para banda 915-928 MHz
- **SAG**: Cumple normas de compatibilidad electromagnética
- **ISO**: Certificación automotriz para componentes vehiculares

---

**Versión Manual**: 1.0  
**Fecha**: Julio 2025  
**Producto**: Sistema Alarma Vehicular ESP32-S3
