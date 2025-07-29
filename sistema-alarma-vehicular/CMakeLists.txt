# CMakeLists.txt - Monorepo Sistema Alarma Vehicular
# Este archivo no se usa directamente. Cada proyecto tiene su propio CMakeLists.txt
# en firmware/llave/CMakeLists.txt y firmware/auto/CMakeLists.txt
#
# Para compilar, usa:
# cd firmware/auto && idf.py build
# cd firmware/llave && idf.py build

cmake_minimum_required(VERSION 3.16)

# message(FATAL_ERROR "No compiles desde este directorio. Usa 'cd firmware/auto && idf.py build' o 'cd firmware/llave && idf.py build'")
