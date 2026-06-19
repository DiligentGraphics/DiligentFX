cmake_minimum_required (VERSION 3.10)

project(RadientGPUTest)

file(GLOB_RECURSE SOURCE src/*.*)
set(MAIN_CPP ../../../DiligentCore/Tests/DiligentCoreAPITest/src/main.cpp)

add_executable(RadientGPUTest ${SOURCE} ${MAIN_CPP})
set_common_target_properties(RadientGPUTest)

target_include_directories(RadientGPUTest
PRIVATE
    ../../Radient/include
)

target_link_libraries(RadientGPUTest
PRIVATE
    Diligent-BuildSettings
    Diligent-TargetPlatform
    Diligent-Common
    Diligent-GraphicsEngine
    Diligent-GPUTestFramework
    DiligentFX
    Radient
    EnTT
    absl::flat_hash_map
)

if (MSVC)
    # Disable warning C4324: structure was padded due to alignment specifier
    target_compile_options(RadientGPUTest PRIVATE /wd4324)
endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCE})
source_group("DiligentCoreAPITest" FILES ${MAIN_CPP})

set_target_properties(RadientGPUTest PROPERTIES
    FOLDER "DiligentFX/Tests"
)

if(PLATFORM_WIN32)
    copy_required_dlls(RadientGPUTest)
endif()
