@echo off
REM Script de compilación para Windows
REM Compila ambos proyectos: llave y auto

echo ==============================================
echo Sistema de Alarma Vehicular - Compilación
echo ==============================================

REM Verificar que ESP-IDF está configurado
if "%IDF_PATH%"=="" (
    echo ERROR: ESP-IDF no está configurado
    echo Ejecuta: call %IDF_PATH%\export.bat
    exit /b 1
)

echo.
echo 🔑 Compilando proyecto LLAVE...
echo ----------------------------------------------
cd firmware\llave
idf.py clean build
if %ERRORLEVEL% neq 0 (
    echo ERROR: Falló compilación de LLAVE
    cd ..\..
    exit /b 1
)
cd ..\..

echo.
echo 🚗 Compilando proyecto AUTO...
echo ----------------------------------------------
cd firmware\auto
idf.py clean build
if %ERRORLEVEL% neq 0 (
    echo ERROR: Falló compilación de AUTO
    cd ..\..
    exit /b 1
)
cd ..\.

echo.
echo ✅ Compilación completa exitosa
echo ----------------------------------------------
echo Binarios generados:
echo   - Llave: firmware\llave\build\llave.bin
echo   - Auto:  firmware\auto\build\auto.bin
echo ==============================================
