cmake_minimum_required(VERSION 3.20)

# Configured through the Radient target in an Emscripten build.
set(MODEL "${CMAKE_CURRENT_BINARY_DIR}/../generated/Radient.json")
set(GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated")

add_custom_target(Radient-GenerateJavaScript
    COMMAND ${BINDING_COMMAND} "${BINDING_TOOLS}/javascript/generate.py"
        --model "${MODEL}" --module Radient --header Radient.h --header RadientMeshPrimitives.h --header NativeAdapters.hpp --header BrowserNative.h
        --adapters "${CMAKE_CURRENT_SOURCE_DIR}/API.json" --output "${GENERATED}"
    DEPENDS Radient-GenerateBindingModel
    BYPRODUCTS "${GENERATED}/Radient.cpp" "${GENERATED}/Radient.mjs"
        "${GENERATED}/Radient.d.mts" "${GENERATED}/runtime.mjs" "${GENERATED}/byte_access.mjs"
    COMMENT "Generating Radient JavaScript bindings"
    VERBATIM)

# Compile the small semantic adapters with Radient so they can reuse its
# private type-size helpers without exposing implementation headers to the glue.
target_sources(Radient PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/NativeAdapters.cpp")
add_executable(RadientWasm EXCLUDE_FROM_ALL "${GENERATED}/Radient.cpp" BrowserNative.cpp)
if(WEBGPU_SUPPORTED)
    target_link_libraries(RadientWasm PRIVATE Diligent-GraphicsEngineWebGPU-static)
    target_link_options(RadientWasm PRIVATE "--js-library=${CMAKE_CURRENT_SOURCE_DIR}/Browser.library.js")
    set_property(TARGET RadientWasm APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Browser.library.js")
endif()
# Keep the browser entry point alongside the generated module. CPU-only builds
# still provide the normal Node/browser bindings; GPU creation returns UNSUPPORTED.
configure_file(RadientBrowser.mjs "${GENERATED}/RadientBrowser.mjs" COPYONLY)
configure_file(RadientBrowser.d.mts "${GENERATED}/RadientBrowser.d.mts" COPYONLY)
target_include_directories(RadientWasm PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
add_dependencies(RadientWasm Radient-GenerateJavaScript)
set_common_target_properties(RadientWasm)
target_link_libraries(RadientWasm PRIVATE Radient Diligent-Common Diligent-BuildSettings)
# Preload workers before module initialization resolves: immediate engine
# destruction can join its workers without waiting for the browser event loop.
set(RADIENT_BINDINGS_PTHREAD_POOL_SIZE "1" CACHE STRING "Number of preloaded Radient Wasm workers")
target_link_options(RadientWasm PRIVATE
    -pthread --no-entry -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,node
    # Shader translation (glslang/Tint) needs more than the default 64 KiB stack.
    -sSTACK_SIZE=2097152
    "$<$<CONFIG:Debug>:-sSTACK_OVERFLOW_CHECK=2>"
    -sALLOW_MEMORY_GROWTH=1 -sASSERTIONS=1
    "-sPTHREAD_POOL_SIZE=${RADIENT_BINDINGS_PTHREAD_POOL_SIZE}"
    "-sEXPORTED_FUNCTIONS=['_malloc','_free']"
    "-sEXPORTED_RUNTIME_METHODS=['HEAPU8','FS','PThread']")
set_target_properties(RadientWasm PROPERTIES SUFFIX ".mjs" RUNTIME_OUTPUT_DIRECTORY "${GENERATED}")

add_subdirectory(tests)

set_target_properties(Radient-GenerateJavaScript RadientWasm PROPERTIES FOLDER "DiligentFX/Bindings")
