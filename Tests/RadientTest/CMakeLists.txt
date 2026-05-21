cmake_minimum_required (VERSION 3.10)

if (PLATFORM_WEB)
    set(CMAKE_EXECUTABLE_SUFFIX ".html")
endif()

project(RadientTest)

file(GLOB_RECURSE SOURCE  src/*.*)

add_executable(RadientTest ${SOURCE})
set_common_target_properties(RadientTest)

target_include_directories(RadientTest
PRIVATE
    ../../Radient/include
)

if (PLATFORM_WEB)  
    set(RESOURCE_PATH "${PROJECT_SOURCE_DIR}/assets/")
    set(HTML_TEMPLATE_FILE ${PROJECT_SOURCE_DIR}/resources/emscripten_template.html)      
    target_link_options(RadientTest PRIVATE "SHELL: -s SINGLE_FILE -s ALLOW_MEMORY_GROWTH=1 --preload-file '${RESOURCE_PATH}@'")      
    target_link_options(RadientTest PRIVATE "SHELL: --shell-file '${HTML_TEMPLATE_FILE}'") 
endif()

target_link_libraries(RadientTest
PRIVATE
    gtest_main
    Diligent-BuildSettings
    Diligent-TargetPlatform
    Diligent-TestFramework
    Diligent-Common
    Radient
    EnTT
)

if(WEBGPU_SUPPORTED)
    target_link_libraries(RadientTest PRIVATE libtint)
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
