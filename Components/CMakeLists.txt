cmake_minimum_required (VERSION 3.6)

set(SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/BoundBoxRenderer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ShadowMapManager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/EnvMapRenderer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/GBuffer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/VectorFieldRenderer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ToneMapping.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/CoordinateGridRenderer.cpp"
)

set(INCLUDE
    "${CMAKE_CURRENT_SOURCE_DIR}/interface/BoundBoxRenderer.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/interface/ShadowMapManager.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/interface/EnvMapRenderer.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/interface/GBuffer.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/interface/VectorFieldRenderer.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/interface/ToneMapping.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/interface/CoordinateGridRenderer.hpp"
)

target_sources(DiligentFX PRIVATE ${SOURCE} ${INCLUDE})

target_include_directories(DiligentFX
PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/interface"
)
