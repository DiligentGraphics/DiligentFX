cmake_minimum_required (VERSION 3.10)

target_sources(DiligentFX PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/interface/DiligentFXShaderSourceStreamFactory.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/DiligentFXShaderSourceStreamFactory.cpp"
)
