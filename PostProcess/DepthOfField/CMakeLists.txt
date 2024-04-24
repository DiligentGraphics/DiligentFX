cmake_minimum_required (VERSION 3.6)

set(SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/DepthOfField.cpp"
)

set(INCLUDE
    "${CMAKE_CURRENT_SOURCE_DIR}/interface/DepthOfField.hpp"
)

target_sources(DiligentFX PRIVATE ${SOURCE} ${INCLUDE})

target_include_directories(DiligentFX
PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/interface"
)
