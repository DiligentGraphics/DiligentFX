cmake_minimum_required (VERSION 3.10)

project(DiligentFXGPUTest)

file(GLOB_RECURSE SOURCE src/*.*)
set(MAIN_CPP ../../../DiligentCore/Tests/DiligentCoreAPITest/src/main.cpp)

add_executable(DiligentFXGPUTest ${SOURCE} ${MAIN_CPP})
set_common_target_properties(DiligentFXGPUTest)

target_link_libraries(DiligentFXGPUTest
PRIVATE
    Diligent-BuildSettings
    Diligent-TargetPlatform
    Diligent-Common
    Diligent-GraphicsEngine
    Diligent-GPUTestFramework
    DiligentFX
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCE})
source_group("DiligentCoreAPITest" FILES ${MAIN_CPP})

set_target_properties(DiligentFXGPUTest
    PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        XCODE_SCHEME_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets"
)

set_target_properties(DiligentFXGPUTest PROPERTIES
    FOLDER "DiligentFX/Tests"
)

if(PLATFORM_WIN32)
    copy_required_dlls(DiligentFXGPUTest)
endif()
