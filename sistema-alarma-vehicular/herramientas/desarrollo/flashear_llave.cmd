@echo off
REM Script de flasheo para llave
REM Uso: flashear_llave.cmd COMx [dispositivo_id]

if "%1"=="" (
    echo Uso: flashear_llave.cmd COMx [dispositivo_id]
    echo Ejemplo: flashear_llave.cmd COM3 LLAVE001
    exit /b 1
)

set PUERTO=%1
set DISPOSITIVO_ID=%2
if "%DISPOSITIVO_ID%"=="" set DISPOSITIVO_ID=LLAVE_DEFAULT

echo ========================================
echo Flasheando LLAVE en puerto %PUERTO%
echo Dispositivo ID: %DISPOSITIVO_ID%
echo ========================================

REM Verificar que ESP-IDF está configurado
if "%IDF_PATH%"=="" (
    echo ERROR: ESP-IDF no está configurado
    exit /b 1
)

REM Verificar puerto
python -c "import serial; serial.Serial('%PUERTO%')" 2>nul
if %ERRORLEVEL% neq 0 (
    echo ERROR: Puerto %PUERTO% no disponible
    exit /b 1
)

REM Ir al directorio del firmware
cd firmware

echo.
echo 1. Compilando firmware llave...
idf.py -D PROYECTO_OBJETIVO=llave -B build_llave build
if %ERRORLEVEL% neq 0 (
    echo ERROR: Falló compilación
    exit /b 1
)

echo.
echo 2. Borrando flash...
esptool.py --chip esp32s3 --port %PUERTO% erase_flash

echo.
echo 3. Flasheando bootloader y particiones...
esptool.py --chip esp32s3 --port %PUERTO% --baud 921600 write_flash ^
    0x0 build_llave\bootloader\bootloader.bin ^
    0x8000 build_llave\partition_table\partition-table.bin ^
    0x20000 build_llave\llave.bin

if %ERRORLEVEL% neq 0 (
    echo ERROR: Falló flasheo
    exit /b 1
)

echo.
echo 4. Programando eFuses...
if exist "..\herramientas\claves\aes_key_%DISPOSITIVO_ID%.bin" (
    echo Programando claves para %DISPOSITIVO_ID%...
    
    REM Programar clave AES en BLK3
    espefuse.py --port %PUERTO% burn_key BLOCK_KEY3 ^
        ..\herramientas\claves\aes_key_%DISPOSITIVO_ID%.bin AES_KEY

    REM Programar clave HMAC en BLK4  
    espefuse.py --port %PUERTO% burn_key BLOCK_KEY4 ^
        ..\herramientas\claves\hmac_key_%DISPOSITIVO_ID%.bin HMAC_DOWN_DIGITAL
        
    echo eFuses programados
) else (
    echo ADVERTENCIA: No se encontraron claves para %DISPOSITIVO_ID%
    echo Ejecuta: python ..\herramientas\configuracion\generar_claves.py -d %DISPOSITIVO_ID%
)

echo.
echo 5. Verificando funcionamiento...
timeout 2 >nul
idf.py -p %PUERTO% monitor --no-reset

echo.
echo ✅ Llave flasheada exitosamente en %PUERTO%
echo ========================================
