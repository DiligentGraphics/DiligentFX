cmake_minimum_required (VERSION 3.6)

set(SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/EpipolarLightScattering.cpp"
)

set(INCLUDE
    "${CMAKE_CURRENT_SOURCE_DIR}/interface/EpipolarLightScattering.h"
)

target_sources(DiligentFX PRIVATE ${SOURCE} ${INCLUDE})

target_include_directories(DiligentFX
PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/interface"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Shaders/PostProcess/EpipolarLightScattering/public"
)
