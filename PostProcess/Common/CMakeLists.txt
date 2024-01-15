cmake_minimum_required (VERSION 3.6)

set(SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/PostFXRenderTechnique.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/PostFXContext.cpp"
)

set(INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/interface/PostFXRenderTechnique.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/interface/PostFXContext.hpp"
)

target_sources(DiligentFX PRIVATE ${SOURCE} ${INTERFACE} ${INCLUDE})

target_include_directories(DiligentFX
PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/interface"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Shaders/PostProcess/Common/public"
)
