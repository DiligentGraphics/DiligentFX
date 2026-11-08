cmake_minimum_required(VERSION 3.13)

# Also usable independently, without configuring the renderer or its dependencies.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(RadientBindingModel CXX)
endif()

find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)

set(RADIENT_BINDINGS_PYTHON_EXECUTABLE "${Python3_EXECUTABLE}" CACHE FILEPATH "Python interpreter with Clang 22 bindings")
set(RADIENT_BINDINGS_LIBCLANG "" CACHE FILEPATH "LLVM 22 shared library (libclang.dll, libclang.so, or libclang.dylib)")
set(RADIENT_BINDINGS_TARGET "" CACHE STRING "Clang target triple override for the binding model (empty uses the configured C++ target)")
set(RADIENT_BINDINGS_CLANG_ARGS "" CACHE STRING "Additional Clang arguments (semicolon-separated)")

get_filename_component(FX_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(BINDING_TOOLS "${FX_SOURCE_ROOT}/BuildTools/Bindings")
set(BINDING_COMMAND "${RADIENT_BINDINGS_PYTHON_EXECUTABLE}" -B)
if(RADIENT_BINDINGS_LIBCLANG)
    set(BINDING_COMMAND "${CMAKE_COMMAND}" -E env
        "LIBCLANG_LIBRARY_FILE=${RADIENT_BINDINGS_LIBCLANG}" ${BINDING_COMMAND})
endif()
set(BINDING_TARGET "${RADIENT_BINDINGS_TARGET}")
if(NOT BINDING_TARGET)
    if(CMAKE_CXX_COMPILER_TARGET)
        set(BINDING_TARGET "${CMAKE_CXX_COMPILER_TARGET}")
    elseif(MSVC)
        # MSVC has no -dumpmachine option. Use the configured architecture,
        # which may differ from the machine running Python (e.g. Win32 on x64).
        if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "ARM64")
            set(BINDING_TARGET "aarch64-pc-windows-msvc")
        elseif(CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "ARM")
            set(BINDING_TARGET "armv7-pc-windows-msvc")
        elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
            set(BINDING_TARGET "i686-pc-windows-msvc")
        else()
            set(BINDING_TARGET "x86_64-pc-windows-msvc")
        endif()
    else()
        execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -dumpmachine
            OUTPUT_VARIABLE BINDING_TARGET
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE BINDING_TARGET_RESULT
        )
        if(NOT BINDING_TARGET_RESULT EQUAL 0 OR NOT BINDING_TARGET)
            message(FATAL_ERROR "Cannot determine the binding target. Set RADIENT_BINDINGS_TARGET to a Clang target triple.")
        endif()
    endif()
endif()

set(BINDING_ARGUMENTS "--target=${BINDING_TARGET}")
if(NOT RADIENT_BINDINGS_TARGET)
    # Share the configured compiler's standard headers. An explicit target
    # override supplies its own paths through RADIENT_BINDINGS_CLANG_ARGS.
    foreach(INCLUDE_DIR IN LISTS CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES)
        list(APPEND BINDING_ARGUMENTS "--clang-arg=-isystem${INCLUDE_DIR}")
    endforeach()
    if(CMAKE_SYSROOT)
        list(APPEND BINDING_ARGUMENTS "--clang-arg=--sysroot=${CMAKE_SYSROOT}")
    endif()
endif()

foreach(ARG IN LISTS RADIENT_BINDINGS_CLANG_ARGS)
    list(APPEND BINDING_ARGUMENTS "--clang-arg=${ARG}")
endforeach()

# The small extraction runs whenever requested, so edits to any transitive header
# are picked up. The generator leaves unchanged output files untouched.
add_custom_target(Radient-GenerateBindingModel
    COMMAND ${BINDING_COMMAND} "${BINDING_TOOLS}/generate.py"
        --source-root "${FX_SOURCE_ROOT}"
        --config "${CMAKE_CURRENT_SOURCE_DIR}/DataBlob.json"
        --output "${CMAKE_CURRENT_BINARY_DIR}/generated/RadientDataBlob.json"
        ${BINDING_ARGUMENTS}
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/generated/RadientDataBlob.json"
    COMMENT "Generating the Radient data-blob binding model"
    VERBATIM
)

add_custom_target(Radient-TestBindingModel
    COMMAND ${BINDING_COMMAND} "${BINDING_TOOLS}/tests/run_tests.py"
        ${BINDING_ARGUMENTS}
    COMMAND ${BINDING_COMMAND} "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_public_api.py"
        ${BINDING_ARGUMENTS}
    COMMENT "Testing Radient binding extraction and metadata"
    VERBATIM
)

set_target_properties(Radient-GenerateBindingModel Radient-TestBindingModel PROPERTIES
    FOLDER "DiligentFX/Bindings"
)
