cmake_minimum_required (VERSION 3.6)

set(SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ShadowMapManager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/EnvMapRenderer.cpp"
)

set(INCLUDE
    "${CMAKE_CURRENT_SOURCE_DIR}/interface/ShadowMapManager.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/interface/EnvMapRenderer.hpp"
)

target_sources(DiligentFX PRIVATE ${SOURCE} ${INCLUDE})

target_include_directories(DiligentFX
PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/interface"
)
