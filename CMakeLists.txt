cmake_minimum_required (VERSION 3.6)

include(BuildUtils.cmake)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

project(DiligentFX CXX)

file(GLOB_RECURSE SHADERS LIST_DIRECTORIES false Shaders/*.*)

add_library(DiligentFX STATIC README.md ${SHADERS})

file(RELATIVE_PATH DILIGENT_FX_DIR "${CMAKE_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")

if(PLATFORM_WIN32 OR PLATFORM_UNIVERSAL_WINDOWS OR PLATFORM_LINUX OR PLATFORM_MACOS OR PLATFORM_IOS OR PLATFORM_TVOS OR PLATFORM_EMSCRIPTEN)
    option(DILIGENT_INSTALL_FX "Install DiligentFX module headers and libraries" ON)
else()
    set(DILIGENT_INSTALL_FX OFF)
endif()

target_link_libraries(DiligentFX 
PRIVATE
    Diligent-BuildSettings
PUBLIC
    Diligent-GraphicsEngine
    Diligent-GraphicsTools
    Diligent-AssetLoader
    Diligent-TextureLoader
    Diligent-Imgui
)

if (TARGET Diligent-HLSL2GLSLConverterLib)
    target_link_libraries(DiligentFX PRIVATE Diligent-HLSL2GLSLConverterLib)
    target_compile_definitions(DiligentFX PRIVATE HLSL2GLSL_CONVERTER_SUPPORTED=1)
endif()

set_common_target_properties(DiligentFX)

target_include_directories(DiligentFX
PUBLIC
    .
)

add_subdirectory(Utilities)
add_subdirectory(Components)
add_subdirectory(PostProcess)
add_subdirectory(PBR)

if(DILIGENT_USD_PATH)
    add_subdirectory(Hydrogent)
endif()

add_subdirectory(Tests)

get_target_property(SOURCE DiligentFX SOURCES)

foreach(FILE ${SOURCE}) 
    # Get the directory of the source file
    get_filename_component(PARENT_DIR "${FILE}" DIRECTORY)

    # Remove common directory prefix to make the group
    string(REPLACE "${CMAKE_CURRENT_SOURCE_DIR}" "" GROUP "${PARENT_DIR}")

    # Make sure we are using windows slashes
    string(REPLACE "/" "\\" GROUP "${GROUP}")

    source_group("${GROUP}" FILES "${FILE}")
endforeach()

# Convert shaders to headers and generate master header with the list of all files
set(SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders_inc)
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})
target_include_directories(DiligentFX PRIVATE ${SHADER_OUTPUT_DIR})

set(SHADERS_LIST_FILE ${SHADER_OUTPUT_DIR}/shaders_list.h)
convert_shaders_to_headers("${SHADERS}" ${SHADER_OUTPUT_DIR} ${SHADERS_LIST_FILE} SHADERS_INC_LIST)

target_sources(DiligentFX PRIVATE
    # A target created in the same directory (CMakeLists.txt file) that specifies any output of the 
    # custom command as a source file is given a rule to generate the file using the command at build time. 
    ${SHADERS_INC_LIST}
    ${SHADERS_LIST_FILE}
)
source_group("generated" FILES
    ${SHADERS_LIST_FILE}
    ${SHADERS_INC_LIST}
)

if(DILIGENT_INSTALL_FX)
    install(TARGETS				 DiligentFX
            ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/${DILIGENT_FX_DIR}/$<CONFIG>"
            LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/${DILIGENT_FX_DIR}/$<CONFIG>"
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}/${DILIGENT_FX_DIR}/$<CONFIG>"
    )
    install(DIRECTORY    PostProcess/EpipolarLightScattering/interface
            DESTINATION  "${CMAKE_INSTALL_INCLUDEDIR}/${DILIGENT_FX_DIR}/PostProcess/EpipolarLightScattering"
    )
    install(DIRECTORY    PostProcess/TemporalAntiAliasing/interface
            DESTINATION  "${CMAKE_INSTALL_INCLUDEDIR}/${DILIGENT_FX_DIR}/PostProcess/TemporalAntiAliasing"
    )
    install(DIRECTORY    PostProcess/ScreenSpaceReflection/interface
            DESTINATION  "${CMAKE_INSTALL_INCLUDEDIR}/${DILIGENT_FX_DIR}/PostProcess/ScreenSpaceReflection"
    )
    install(DIRECTORY   PostProcess/ScreenSpaceAmbientOcclusion/interface
            DESTINATION  "${CMAKE_INSTALL_INCLUDEDIR}/${DILIGENT_FX_DIR}/PostProcess/ScreenSpaceAmbientOcclusion"
    )
    install(DIRECTORY   PostProcess/Bloom/interface
            DESTINATION  "${CMAKE_INSTALL_INCLUDEDIR}/${DILIGENT_FX_DIR}/PostProcess/Bloom"
    )
    install(DIRECTORY    Components/interface
            DESTINATION  "${CMAKE_INSTALL_INCLUDEDIR}/${DILIGENT_FX_DIR}/Components"
    )
    install(DIRECTORY    PBR/interface
            DESTINATION  "${CMAKE_INSTALL_INCLUDEDIR}/${DILIGENT_FX_DIR}/PBR"
    )
    install(DIRECTORY    Shaders
            DESTINATION  "."
            FILES_MATCHING PATTERN "public/*.*"
            PATTERN "private" EXCLUDE
    )
endif()

set_target_properties(DiligentFX PROPERTIES
    FOLDER DiligentFX
)

# Create a custom target to run source code formatting validation command
add_format_validation_target(DiligentFX "${CMAKE_CURRENT_SOURCE_DIR}" DiligentFX)
