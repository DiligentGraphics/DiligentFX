cmake_minimum_required (VERSION 3.6)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

project(DiligentFX CXX)

file(GLOB_RECURSE SHADERS LIST_DIRECTORIES false Shaders/*.*)

add_library(DiligentFX STATIC README.md ${SHADERS})

target_link_libraries(DiligentFX 
PUBLIC
    BuildSettings
    GraphicsEngine
    GraphicsTools
    AssetLoader
)
set_common_target_properties(DiligentFX)

# Create custom target for custom commands such as converting shaders to headers
add_custom_target(DiligentFXCustomCommands)
add_dependencies(DiligentFX DiligentFXCustomCommands)
set_target_properties(DiligentFX DiligentFXCustomCommands PROPERTIES
    FOLDER DiligentFX
)

target_include_directories(DiligentFX PUBLIC
    Shaders/Common/public
)

add_subdirectory(Utilities)
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
    message(WARNING "File2String utility is currently unavailable on this host system. This is not an issues unless you modify epipolar light scattering shaders")
endif()
