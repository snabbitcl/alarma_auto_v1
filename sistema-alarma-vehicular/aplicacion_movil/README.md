# Aplicación Móvil - Sistema de Alarma Vehicular

## Descripción

Aplicación móvil complementaria para el Sistema de Alarma Vehicular ESP32-S3.

## Funcionalidades Planificadas

### Versión 1.0
- [ ] Conexión BLE con llave electrónica
- [ ] Control básico del vehículo
- [ ] Monitoreo de estado en tiempo real
- [ ] Notificaciones push

### Versión 1.5
- [ ] Geolocalización del vehículo
- [ ] Historial de eventos
- [ ] Configuración avanzada
- [ ] Múltiples vehículos

### Características Técnicas

#### Plataformas Objetivo
- **Android**: API 21+ (Android 5.0+)
- **iOS**: iOS 12.0+
- **Framework**: React Native / Flutter

#### Comunicación
- **BLE 5.0**: Conexión directa con llave
- **HTTPS**: API REST para telemetría
- **Push**: Firebase Cloud Messaging

#### Seguridad
- **Autenticación**: Biométrica + PIN
- **Cifrado**: End-to-end con llave
- **Certificados**: Certificate pinning

## Desarrollo

### Requisitos
```bash
# React Native
npm install -g react-native-cli
npm install

# o Flutter
flutter pub get
```

### Estructura
```
app/
├── src/
│   ├── components/
│   ├── screens/
│   ├── services/
│   └── utils/
├── assets/
├── android/
├── ios/
└── package.json
```

### Estado
🚧 **En desarrollo** - Esperando implementación

## Contribución

Para contribuir al desarrollo de la aplicación móvil:

1. Fork del repositorio
2. Crear rama feature
3. Implementar funcionalidad
4. Tests unitarios
5. Pull request

## Contacto

Para más información sobre el desarrollo de la app móvil:
- Email: dev@alarma-vehicular.com
- Issues: GitHub repository
