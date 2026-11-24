cmake_minimum_required (VERSION 3.10)

project(RadientTest)

file(GLOB_RECURSE SOURCE  src/*.*)

if(PLATFORM_ANDROID)
    add_subdirectory(android)
    add_library(RadientTest SHARED ${SOURCE} android/TestEntry.cpp network/android/TLSTimeoutTest.cpp)
    target_compile_definitions(RadientTest PRIVATE RADIENT_ANDROID_NETWORK_TEST=1)
else()
    add_executable(RadientTest ${SOURCE})
endif()
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
    gtest
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

if(PLATFORM_WEB)
    target_sources(RadientTest PRIVATE network/RadientNetworkAssetResolverWebTest.cpp)
elseif(WIN32 AND ARCH EQUAL 32)
    # cpp-httplib explicitly does not support 32-bit Windows.
    message(STATUS "Skipping Radient HTTP-server tests: cpp-httplib does not support 32-bit Windows")
elseif(NOT PLATFORM_WEB)
    # Keep the test server and its optional dependencies out of production builds.
    include(network/HttpServer.cmake)
    target_sources(RadientTest PRIVATE network/RadientNetworkAssetResolverTest.cpp)
    target_link_libraries(RadientTest PRIVATE httplib::httplib ZLIB::ZLIB)
endif()
