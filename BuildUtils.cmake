
# Converts shaders to headers and generates master header with the list of all files
function(convert_shaders_to_headers _SHADERS _SHADER_OUTPUT_DIR _SHADERS_LIST_FILE _SHADERS_INC_LIST)
    if(NOT FILE2STRING_PATH STREQUAL "")
        find_package(Python3 REQUIRED)

        file(MAKE_DIRECTORY ${_SHADER_OUTPUT_DIR})

        file(WRITE ${_SHADERS_LIST_FILE}
            "static const MemoryShaderSourceFileInfo g_Shaders[] =\n"
            "{"
            )

        foreach(FILE ${_SHADERS})
            get_filename_component(FILE_NAME ${FILE} NAME)
            set(CONVERTED_FILE ${_SHADER_OUTPUT_DIR}/${FILE_NAME}.h)
            add_custom_command(OUTPUT ${CONVERTED_FILE}
                               COMMAND ${Python3_EXECUTABLE} ${FILE2STRING_PATH} ${FILE} ${CONVERTED_FILE}
                               MAIN_DEPENDENCY ${FILE} # the primary input source file to the command
                               COMMENT "Processing shader ${FILE}"
                               VERBATIM)

            string(REPLACE "." "_" VAR_NAME "${FILE_NAME}")
            file(APPEND ${_SHADERS_LIST_FILE}
                    "\n    {"
                    "\n        \"${FILE_NAME}\","
                    "\n        #include \"${FILE_NAME}.h\""
                    "\n    },"
                )

                list(APPEND SHADERS_INC_LIST ${CONVERTED_FILE})
        endforeach()

        file(APPEND ${_SHADERS_LIST_FILE}
            "\n};\n"
            )

        set_source_files_properties(
            ${SHADERS_INC_LIST}
            PROPERTIES GENERATED TRUE
        )

        set(${_SHADERS_INC_LIST} ${SHADERS_INC_LIST} PARENT_SCOPE)
    else()
        message(WARNING "File2String utility is currently unavailable on this host system. This is not an issues unless you modify shaders")
    endif()
endfunction()
