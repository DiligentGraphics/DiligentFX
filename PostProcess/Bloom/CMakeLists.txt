cmake_minimum_required (VERSION 3.10)

set(SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Bloom.cpp"
)

set(INCLUDE
    "${CMAKE_CURRENT_SOURCE_DIR}/interface/Bloom.hpp"
)

target_sources(DiligentFX PRIVATE ${SOURCE} ${INCLUDE})

target_include_directories(DiligentFX
PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/interface"
)
