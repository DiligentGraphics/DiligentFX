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
add_dependencies(RadientWasm Radient-GenerateJavaScript)

set(RADIENT_JAVASCRIPT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
# Preload workers so engine shutdown can join without waiting on browser startup.
set(RADIENT_BINDINGS_PTHREAD_POOL_SIZE "1" CACHE STRING "Number of preloaded Radient Wasm workers")

# Both Wasm modules link Radient and its shader translation libraries. Limit
# Ninja to one of these memory-intensive links at a time, while allowing
# compilation to use the requested build parallelism.
set_property(GLOBAL APPEND PROPERTY JOB_POOLS radient_wasm_link=1)

# Production bindings and the render-test executable share the same native
# library and module settings. Test-only image helpers stay out of RadientWasm.
function(configure_radient_wasm_target Target OutputDirectory)
    set_common_target_properties(${Target})
    set_property(TARGET ${Target} PROPERTY JOB_POOL_LINK radient_wasm_link)
    target_include_directories(${Target} PRIVATE "${RADIENT_JAVASCRIPT_SOURCE_DIR}")
    target_link_libraries(${Target} PRIVATE Radient Diligent-Common Diligent-BuildSettings)
    if(WEBGPU_SUPPORTED)
        target_link_libraries(${Target} PRIVATE Diligent-GraphicsEngineWebGPU-static)
        target_link_options(${Target} PRIVATE "--js-library=${RADIENT_JAVASCRIPT_SOURCE_DIR}/Browser.library.js")
        set_property(TARGET ${Target} APPEND PROPERTY LINK_DEPENDS "${RADIENT_JAVASCRIPT_SOURCE_DIR}/Browser.library.js")
    endif()
    foreach(File RadientBrowser.mjs RadientBrowser.d.mts)
        configure_file("${RADIENT_JAVASCRIPT_SOURCE_DIR}/${File}" "${OutputDirectory}/${File}" COPYONLY)
    endforeach()
    # Enable the public ModuleOptions fields and the initialization hooks used
    # by the bindings. The shared response file documents each option.
    set_property(TARGET ${Target} APPEND PROPERTY LINK_DEPENDS "${BINDING_TOOLS}/javascript/IncomingModuleAPI.txt")
    target_link_options(${Target} PRIVATE
        "-sINCOMING_MODULE_JS_API=@${BINDING_TOOLS}/javascript/IncomingModuleAPI.txt"
        -pthread --no-entry -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,node
        # Shader translation (glslang/Tint) needs more than the default 64 KiB stack.
        -sSTACK_SIZE=2097152
        "$<$<CONFIG:Debug>:-sSTACK_OVERFLOW_CHECK=2>"
        -sALLOW_MEMORY_GROWTH=1 -sASSERTIONS=1
        "-sPTHREAD_POOL_SIZE=${RADIENT_BINDINGS_PTHREAD_POOL_SIZE}"
        "-sEXPORTED_FUNCTIONS=['_malloc','_free']"
        "-sEXPORTED_RUNTIME_METHODS=['HEAPU8','FS','PThread']")
    set_target_properties(${Target} PROPERTIES SUFFIX ".mjs" RUNTIME_OUTPUT_DIRECTORY "${OutputDirectory}")
endfunction()
configure_radient_wasm_target(RadientWasm "${GENERATED}")

add_subdirectory(tests)

set_target_properties(Radient-GenerateJavaScript RadientWasm PROPERTIES FOLDER "DiligentFX/Bindings")
