cmake_minimum_required (VERSION 3.6)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

project(DiligentFX CXX)

file(GLOB_RECURSE SHADERS LIST_DIRECTORIES false Shaders/*.*)

add_library(DiligentFX STATIC README.md ${SHADERS})

file(RELATIVE_PATH DILIGENT_FX_DIR "${CMAKE_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")

if(PLATFORM_WIN32 OR PLATFORM_UNIVERSAL_WINDOWS OR PLATFORM_LINUX OR PLATFORM_MACOS OR PLATFORM_IOS)
    option(DILIGENT_INSTALL_FX "Install DiligentFX module headers and libraries" ON)
else()
    set(DILIGENT_INSTALL_FX OFF)
endif()

target_link_libraries(DiligentFX 
PUBLIC
    Diligent-BuildSettings
    Diligent-GraphicsEngine
    Diligent-GraphicsTools
    Diligent-AssetLoader
)
set_common_target_properties(DiligentFX)

# Create custom target for custom commands such as converting shaders to headers
add_custom_target(DiligentFXCustomCommands)
add_dependencies(DiligentFX DiligentFXCustomCommands)
set_target_properties(DiligentFX DiligentFXCustomCommands PROPERTIES
    FOLDER DiligentFX
)

target_include_directories(DiligentFX
PUBLIC
    .
)

add_subdirectory(Utilities)
add_subdirectory(Components)
add_subdirectory(Postprocess)
add_subdirectory(GLTF_PBR_Renderer)

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
if(NOT FILE2STRING_PATH STREQUAL "")
    set(SHADER_OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders_inc)

    file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})

    set(SHADERS_LIST_FILE shaders_inc/shaders_list.h)
    file(WRITE ${SHADERS_LIST_FILE}
        "struct ShaderIncInfo\n"
        "{\n"
        "    const char* const FileName;\n"
        "    const char* const Source;\n"
        "};\n"
        "\n"
        "static const ShaderIncInfo g_Shaders[] =\n"
        "{"
        )

    foreach(FILE ${SHADERS})

        get_filename_component(FILE_NAME ${FILE} NAME)
        add_custom_command(TARGET DiligentFXCustomCommands 
                           # Unfortunately it is not possible to set TARGET directly to DiligentFX
                           # because PRE_BUILD is only supported on Visual Studio 8 or later. For all other generators 
                           # PRE_BUILD is treated as PRE_LINK.
                           COMMAND ${FILE2STRING_PATH} ${FILE} ${SHADER_OUTPUT_DIR}/${FILE_NAME}.h
                           MAIN_DEPENDENCY ${FILE} # the primary input source file to the command
                           COMMENT "Processing shader ${FILE}"
                           VERBATIM)

        string(REPLACE "." "_" VAR_NAME "${FILE_NAME}")
        file(APPEND ${SHADERS_LIST_FILE}
                "\n    {"
                "\n        \"${FILE_NAME}\","
                "\n        #include \"${FILE_NAME}.h\""
                "\n    },"
            )

    endforeach()
    file(APPEND ${SHADERS_LIST_FILE}
        "\n};\n"
        )
else()
    message(WARNING "File2String utility is currently unavailable on this host system. This is not an issues unless you modify shaders")
endif()


if(DILIGENT_INSTALL_FX)
    install(TARGETS				 DiligentFX
            ARCHIVE DESTINATION "lib/${DILIGENT_FX_DIR}/$<CONFIG>"
            LIBRARY DESTINATION "bin/${DILIGENT_FX_DIR}/$<CONFIG>"
            RUNTIME DESTINATION "bin/${DILIGENT_FX_DIR}/$<CONFIG>"
    )
    install(DIRECTORY    PostProcess/EpipolarLightScattering/interface
            DESTINATION  "headers/${DILIGENT_FX_DIR}/PostProcess/EpipolarLightScattering"
    )
    install(DIRECTORY    Components/interface
            DESTINATION  "headers/${DILIGENT_FX_DIR}/Components"
    )
    install(DIRECTORY    GLTF_PBR_Renderer/interface
            DESTINATION  "headers/${DILIGENT_FX_DIR}/GLTF_PBR_Renderer"
    )
    install(DIRECTORY    Shaders
            DESTINATION  "."
            FILES_MATCHING PATTERN "public/*.*"
            PATTERN "private" EXCLUDE
    )
endif()


# Create a custom target to run source code formatting validation command
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    add_custom_target(DiligentFX-ValidateFormatting ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/../DiligentCore/.clang-format" "${CMAKE_CURRENT_SOURCE_DIR}/.clang-format"
    )

    add_custom_command(TARGET DiligentFX-ValidateFormatting
        COMMAND validate_format_win.bat
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/BuildTools/FormatValidation"
        COMMENT "Validating DiligentFX module's source code formatting..."
        VERBATIM
    )
endif()

if(TARGET DiligentFX-ValidateFormatting)
    set_target_properties(DiligentFX-ValidateFormatting PROPERTIES
        FOLDER DiligentFX
    )
endif()
