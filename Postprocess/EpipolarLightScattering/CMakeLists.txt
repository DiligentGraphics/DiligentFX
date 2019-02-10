cmake_minimum_required (VERSION 3.6)

set(SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/EpipolarLightScattering.cpp"
)

set(INCLUDE
    "${CMAKE_CURRENT_SOURCE_DIR}/interface/EpipolarLightScattering.h"
)

file(GLOB SHADERS LIST_DIRECTORIES false shaders/*.* shaders/precompute/*.*)
list(APPEND SHADERS "${CMAKE_CURRENT_SOURCE_DIR}/interface/Structures.fxh")

target_sources(DiligentFX PRIVATE ${SOURCE} ${INCLUDE} ${SHADERS})

list(APPEND SHADER_TO_HEADER_LIST ${SHADERS})
SET(SHADER_TO_HEADER_LIST "${SHADER_TO_HEADER_LIST}" CACHE INTERNAL "List of shaders to convert to headers")

target_include_directories(DiligentFX
PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/interface"
)
