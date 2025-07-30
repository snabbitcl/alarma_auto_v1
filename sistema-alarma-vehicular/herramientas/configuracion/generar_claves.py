#!/usr/bin/env python3
"""
Generador de claves para el sistema de alarma vehicular
Genera claves AES y HMAC únicas para cada dispositivo
"""

import os
import sys
import secrets
import hashlib
import tempfile
import subprocess
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.kdf.hkdf import HKDF
import argparse

def generar_claves_dispositivo(dispositivo_id, clave_maestra=None):
    """
    Genera claves AES y HMAC para un dispositivo específico
    """
    if clave_maestra is None:
        clave_maestra = secrets.token_bytes(32)
        print(f"Clave maestra generada: {clave_maestra.hex()}")
    
    # Derivar claves usando HKDF
    hkdf = HKDF(
        algorithm=hashes.SHA256(),
        length=32,
        salt=b'sistema-alarma-vehicular-v1',
        info=f'dispositivo-{dispositivo_id}'.encode()
    )
    
    material_derivado = hkdf.derive(clave_maestra)
    
    # Dividir en clave AES (16 bytes) y HMAC (32 bytes)
    clave_aes = material_derivado[:16]
    
    # Para HMAC, usar toda la clave derivada
    hkdf_hmac = HKDF(
        algorithm=hashes.SHA256(),
        length=32,
        salt=b'hmac-key-derivation',
        info=f'hmac-{dispositivo_id}'.encode()
    )
    clave_hmac = hkdf_hmac.derive(clave_maestra)
    
    return clave_aes, clave_hmac

def guardar_claves_archivo(dispositivo_id, clave_aes, clave_hmac, directorio='claves'):
    """
    Guarda las claves en archivos para flasheo
    """
    os.makedirs(directorio, exist_ok=True)
    
    # Guardar clave AES
    with open(f'{directorio}/aes_key_{dispositivo_id}.bin', 'wb') as f:
        f.write(clave_aes)
    
    # Guardar clave HMAC
    with open(f'{directorio}/hmac_key_{dispositivo_id}.bin', 'wb') as f:
        f.write(clave_hmac)
    
    # Guardar información legible
    with open(f'{directorio}/keys_info_{dispositivo_id}.txt', 'w') as f:
        f.write(f"Dispositivo ID: {dispositivo_id}\n")
        f.write(f"Clave AES (hex): {clave_aes.hex()}\n")
        f.write(f"Clave HMAC (hex): {clave_hmac.hex()}\n")
        f.write(f"Timestamp generación: {hash(os.urandom(8))}\n")

def escribir_claves_nvs(dispositivo_id, clave_aes, clave_hmac, directorio='claves'):
    """Genera una imagen NVS con las claves"""
    os.makedirs(directorio, exist_ok=True)

    csv_path = os.path.join(directorio, f'nvs_{dispositivo_id}.csv')
    bin_path = os.path.join(directorio, f'nvs_{dispositivo_id}.bin')

    with open(csv_path, 'w') as f:
        f.write('namespace,secure_keys\n')
        f.write(f'aes,data,hex2bin,{clave_aes.hex()}\n')
        f.write(f'hmac,data,hex2bin,{clave_hmac.hex()}\n')

    try:
        subprocess.run(['nvs_partition_gen.py', csv_path, bin_path, '0x1000'], check=True)
    finally:
        os.remove(csv_path)

def generar_lote_dispositivos(cantidad, prefijo='DEV'):
    """
    Genera claves para múltiples dispositivos
    """
    clave_maestra = secrets.token_bytes(32)
    
    print(f"Generando claves para {cantidad} dispositivos...")
    print(f"Clave maestra: {clave_maestra.hex()}")
    
    for i in range(cantidad):
        dispositivo_id = f"{prefijo}{i:04d}"
        clave_aes, clave_hmac = generar_claves_dispositivo(dispositivo_id, clave_maestra)
        guardar_claves_archivo(dispositivo_id, clave_aes, clave_hmac)
        escribir_claves_nvs(dispositivo_id, clave_aes, clave_hmac)
        print(f"  ✓ {dispositivo_id}")
    
    # Guardar clave maestra
    with open('claves/master_key.bin', 'wb') as f:
        f.write(clave_maestra)
    
    print(f"\nClaves generadas en directorio 'claves/'")

def main():
    parser = argparse.ArgumentParser(description='Generador de claves para alarma vehicular')
    parser.add_argument('--dispositivo', '-d', help='ID del dispositivo individual')
    parser.add_argument('--lote', '-l', type=int, help='Cantidad de dispositivos en lote')
    parser.add_argument('--prefijo', '-p', default='DEV', help='Prefijo para dispositivos en lote')
    
    args = parser.parse_args()
    
    if args.dispositivo:
        # Generar para un dispositivo
        clave_aes, clave_hmac = generar_claves_dispositivo(args.dispositivo)
        guardar_claves_archivo(args.dispositivo, clave_aes, clave_hmac)
        escribir_claves_nvs(args.dispositivo, clave_aes, clave_hmac)
        print(f"Claves generadas para {args.dispositivo}")
        print(f"AES: {clave_aes.hex()}")
        print(f"HMAC: {clave_hmac.hex()}")
        
    elif args.lote:
        # Generar lote
        generar_lote_dispositivos(args.lote, args.prefijo)
        
    else:
        print("Especifica --dispositivo o --lote")
        parser.print_help()

if __name__ == "__main__":
    main()
