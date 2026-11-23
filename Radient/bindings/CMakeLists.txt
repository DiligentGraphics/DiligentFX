cmake_minimum_required(VERSION 3.13)

# Also usable independently, without configuring the renderer or its dependencies.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(RadientBindingModel CXX)
endif()

# Keep binding tests scoped to this directory even in the full engine build.
# Run CTest with --test-dir pointing at the corresponding bindings build directory.
enable_testing()

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
    if(EMSCRIPTEN)
        set(BINDING_TARGET "wasm32-unknown-emscripten")
    elseif(CMAKE_CXX_COMPILER_TARGET)
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
    # Native API headers use Diligent's explicit platform macros. Match the
    # library rather than relying on Clang's built-in operating-system macros.
    if(TARGET Diligent-PublicBuildSettings)
        get_target_property(BINDING_DEFINITIONS Diligent-PublicBuildSettings INTERFACE_COMPILE_DEFINITIONS)
        foreach(DEFINITION IN LISTS BINDING_DEFINITIONS)
            if(DEFINITION MATCHES "^PLATFORM_[A-Z0-9_]+=1$")
                list(APPEND BINDING_ARGUMENTS "--clang-arg=-D${DEFINITION}")
            endif()
        endforeach()
    endif()
    if(EMSCRIPTEN)
        list(APPEND BINDING_ARGUMENTS
            "--clang-arg=--sysroot=${EMSCRIPTEN_SYSROOT}"
            "--clang-arg=-isystem${EMSCRIPTEN_SYSROOT}/include/c++/v1"
            --clang-arg=-DPLATFORM_WEB=1)
    endif()
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
set(BINDING_API "${CMAKE_CURRENT_SOURCE_DIR}/API.json")
if(PLATFORM_ANDROID)
    set(BINDING_API "${CMAKE_CURRENT_SOURCE_DIR}/AndroidAPI.json")
endif()
add_custom_target(Radient-GenerateBindingModel
    COMMAND ${BINDING_COMMAND} "${BINDING_TOOLS}/generate.py"
        --source-root "${FX_SOURCE_ROOT}"
        --config "${BINDING_API}"
        --output "${CMAKE_CURRENT_BINARY_DIR}/generated/Radient.json"
        ${BINDING_ARGUMENTS}
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/generated/Radient.json"
    COMMENT "Generating the Radient binding model"
    VERBATIM
)

add_test(NAME Bindings-Model
    COMMAND ${BINDING_COMMAND} "${BINDING_TOOLS}/tests/run_tests.py"
        ${BINDING_ARGUMENTS})
add_test(NAME Radient-BindingModel
    COMMAND ${BINDING_COMMAND} "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_public_api.py" -v
        --config "${BINDING_API}" ${BINDING_ARGUMENTS})
set_tests_properties(Bindings-Model Radient-BindingModel PROPERTIES LABELS "Bindings;Model")

set_target_properties(Radient-GenerateBindingModel PROPERTIES FOLDER "DiligentFX/Bindings")

# Compile portable semantic adapters with Radient so all language backends can
# reuse its private type-size helpers without exposing them in generated glue.
if(TARGET Radient)
    target_sources(Radient PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/NativeAdapters.cpp")
endif()

# JavaScript bindings link the production library. Standalone configurations
# provide only model generation and tests, without building Radient's dependencies.
if(EMSCRIPTEN AND TARGET Radient)
    add_subdirectory(javascript)
endif()

if(RADIENT_BUILD_JAVA_BINDINGS)
    if((CMAKE_CROSSCOMPILING AND NOT PLATFORM_ANDROID) OR EMSCRIPTEN OR NOT TARGET Radient)
        message(FATAL_ERROR "Radient Java bindings require Android or a native Radient build matching the host JVM architecture.")
    endif()
    add_subdirectory(java)
endif()
