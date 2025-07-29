cmake_minimum_required(VERSION 3.16)

# Configurar directorios de componentes
set(EXTRA_COMPONENT_DIRS 
    ${CMAKE_CURRENT_LIST_DIR}/../compartido
    ${CMAKE_CURRENT_LIST_DIR}/componentes/actuadores
    ${CMAKE_CURRENT_LIST_DIR}/componentes/sensores
    ${CMAKE_CURRENT_LIST_DIR}/componentes/seguridad
    ${CMAKE_CURRENT_LIST_DIR}/componentes/comunicacion
    ${CMAKE_CURRENT_LIST_DIR}/controladores/reles
    ${CMAKE_CURRENT_LIST_DIR}/controladores/sensores
    ${CMAKE_CURRENT_LIST_DIR}/controladores/sx1276
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(auto)
