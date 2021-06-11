cmake_minimum_required (VERSION 3.6)

project(Diligent-FrameGraph CXX)

set(INTERFACE 
    interface/FrameGraph.h
    interface/FrameGraphBase.hpp
)

set(INCLUDE
    include/FrameGraphImpl.hpp
)

set(SOURCE
    src/FrameGraphImpl.cpp
)

add_library(Diligent-FrameGraph STATIC ${SOURCE} ${INTERFACE} ${INCLUDE})

target_include_directories(Diligent-FrameGraph 
PUBLIC
    interface
PRIVATE
    include
)

target_link_libraries(Diligent-FrameGraph 
PRIVATE
    Diligent-BuildSettings
PUBLIC
    Diligent-Common 
    Diligent-GraphicsEngineInterface
)

set_common_target_properties(Diligent-FrameGraph)

source_group("src" FILES ${SOURCE})
source_group("include" FILES ${INCLUDE})
source_group("interface" FILES ${INTERFACE})

set_target_properties(Diligent-FrameGraph PROPERTIES
    FOLDER DiligentFX
)
