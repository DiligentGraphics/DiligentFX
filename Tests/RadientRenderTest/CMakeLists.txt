cmake_minimum_required (VERSION 3.10)

project(RadientRenderTest)

file(GLOB_RECURSE SOURCE src/*.*)
file(GLOB_RECURSE ASSETS assets/*.*)

get_target_property(DILIGENT_NATIVE_APP_SOURCE_DIR Diligent-NativeAppBase SOURCE_DIR)

add_executable(RadientRenderTest ${SOURCE} ${ASSETS})
set_common_target_properties(RadientRenderTest)

target_compile_definitions(RadientRenderTest
PRIVATE
    RADIENT_RENDER_TEST_MANIFEST_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets/RadientRenderTests.json"
)

target_include_directories(RadientRenderTest
PRIVATE
    ../../Radient/include
    ${DILIGENT_NATIVE_APP_SOURCE_DIR}/include
)

target_link_libraries(RadientRenderTest
PRIVATE
    Diligent-BuildSettings
    Diligent-TargetPlatform
    Diligent-Common
    Diligent-GraphicsEngine
    Diligent-GPUTestFramework
    Diligent-JSON
    DiligentFX
    Radient
    EnTT
    absl::flat_hash_map
)

if (MSVC)
    # Disable warning C4324: structure was padded due to alignment specifier
    target_compile_options(RadientRenderTest PRIVATE /wd4324)
endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCE})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${ASSETS})

set_target_properties(RadientRenderTest PROPERTIES
    FOLDER "DiligentFX/Tests"
)

if(PLATFORM_WIN32)
    copy_required_dlls(RadientRenderTest)
endif()
