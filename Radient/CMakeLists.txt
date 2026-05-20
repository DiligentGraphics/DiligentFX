cmake_minimum_required (VERSION 3.13)

project(Radient CXX)

set(SOURCE
    src/RadientSceneImpl.cpp
    src/RadientSceneWriterImpl.cpp
)

set(INCLUDE
    include/RadientSceneImpl.hpp
    include/RadientSceneWriterImpl.hpp
)

set(INTERFACE
    interface/RadientMath.h
    interface/RadientScene.h
    interface/RadientSceneWriter.h
    interface/RadientTypes.h
)

set(RADIENT_SOURCE ${SOURCE} ${INCLUDE} ${INTERFACE} readme.md)
add_library(Radient STATIC ${RADIENT_SOURCE})

set_common_target_properties(Radient)

target_include_directories(Radient
PUBLIC
    interface
PRIVATE
    include
)

target_link_libraries(Radient
PRIVATE
    Diligent-BuildSettings 
    Diligent-GraphicsEngine
    Diligent-BasicPlatform
    Diligent-GraphicsTools
    Diligent-Common
    EnTT
)

set_target_properties(Radient PROPERTIES
    FOLDER DiligentFX
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${RADIENT_SOURCE})
