cmake_minimum_required (VERSION 3.10)

project(RadientTest)

file(GLOB_RECURSE SOURCE  src/*.*)

add_executable(RadientTest ${SOURCE})
set_common_target_properties(RadientTest)
configure_radient_browser_test(RadientTest)

target_include_directories(RadientTest
PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ../../Radient/include
    ../../Radient/src
    ../RadientCommon/include
)

target_link_libraries(RadientTest
PRIVATE
    gtest_main
    Diligent-BuildSettings
    Diligent-TargetPlatform
    Diligent-TestFramework
    Diligent-Common
    Diligent-AssetLoader
    DiligentFX
    Radient
    EnTT
    absl::flat_hash_map
    absl::inlined_vector
)

if (MSVC)
    # Disable warning C4324: structure was padded due to alignment specifier
    target_compile_options(RadientTest PRIVATE /wd4324)
endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCE})

set_target_properties(RadientTest
    PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        XCODE_SCHEME_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets"
)

set_target_properties(RadientTest PROPERTIES
    FOLDER "DiligentFX/Tests"
)
