cmake_minimum_required(VERSION 3.16)

# Configurar directorios de componentes
set(EXTRA_COMPONENT_DIRS 
    ${CMAKE_CURRENT_LIST_DIR}/../compartido
    ${CMAKE_CURRENT_LIST_DIR}/componentes
    ${CMAKE_CURRENT_LIST_DIR}/controladores
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(llave)
