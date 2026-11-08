cmake_minimum_required(VERSION 3.20)

# Configured through the Radient target in an Emscripten build.
find_program(RADIENT_BINDINGS_NODE_EXECUTABLE NAMES node REQUIRED)
set(MODEL "${CMAKE_CURRENT_BINARY_DIR}/../generated/Radient.json")
set(GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated")

add_custom_target(Radient-GenerateJavaScript
    COMMAND ${BINDING_COMMAND} "${BINDING_TOOLS}/javascript/generate.py"
        --model "${MODEL}" --module Radient --header RadientDataBlob.h --header RadientEngine.h
        --adapters "${CMAKE_CURRENT_SOURCE_DIR}/API.json" --output "${GENERATED}"
    DEPENDS Radient-GenerateBindingModel
    BYPRODUCTS "${GENERATED}/Radient.cpp" "${GENERATED}/Radient.mjs"
        "${GENERATED}/Radient.d.mts" "${GENERATED}/runtime.mjs" "${GENERATED}/byte_access.mjs"
    COMMENT "Generating Radient JavaScript bindings"
    VERBATIM)

add_executable(RadientWasm EXCLUDE_FROM_ALL "${GENERATED}/Radient.cpp")
add_dependencies(RadientWasm Radient-GenerateJavaScript)
set_common_target_properties(RadientWasm)
target_link_libraries(RadientWasm PRIVATE Radient Diligent-BuildSettings)
# Preload workers before module initialization resolves: immediate engine
# destruction can join its workers without waiting for the browser event loop.
set(RADIENT_BINDINGS_PTHREAD_POOL_SIZE "1" CACHE STRING "Number of preloaded Radient Wasm workers")
target_link_options(RadientWasm PRIVATE
    -pthread --no-entry -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,node
    -sALLOW_MEMORY_GROWTH=1 -sASSERTIONS=1
    "-sPTHREAD_POOL_SIZE=${RADIENT_BINDINGS_PTHREAD_POOL_SIZE}"
    "-sEXPORTED_FUNCTIONS=['_malloc','_free']"
    "-sEXPORTED_RUNTIME_METHODS=['HEAPU8']")
set_target_properties(RadientWasm PROPERTIES SUFFIX ".mjs" RUNTIME_OUTPUT_DIRECTORY "${GENERATED}")

# Build RadientWasm first; these tests never generate or rebuild artifacts.
add_test(NAME Radient-JavaScriptGeneration
    COMMAND ${BINDING_COMMAND} "${CMAKE_CURRENT_SOURCE_DIR}/test_generation.py" "${MODEL}" -v)
set_tests_properties(Radient-JavaScriptGeneration PROPERTIES LABELS "Bindings;JavaScript")

# Each suite runs unchanged against the real library in Node and a browser.
file(GLOB SUITES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*_tests.mjs")
foreach(SUITE IN LISTS SUITES)
    get_filename_component(SUITE_NAME "${SUITE}" NAME_WE)
    add_test(NAME "Radient-JavaScript-Node-${SUITE_NAME}"
        COMMAND "${RADIENT_BINDINGS_NODE_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/run_node.mjs" "${GENERATED}/Radient.mjs" "${SUITE}")
    add_test(NAME "Radient-JavaScript-Browser-${SUITE_NAME}"
        COMMAND ${BINDING_COMMAND} "${BINDING_TOOLS}/javascript/tests/run_browser_tests.py"
            --module "${GENERATED}/Radient.mjs" --suite "${SUITE}" --shared-memory)
    set_tests_properties("Radient-JavaScript-Node-${SUITE_NAME}"
        PROPERTIES LABELS "Bindings;JavaScript" TIMEOUT 90)
    set_tests_properties("Radient-JavaScript-Browser-${SUITE_NAME}"
        PROPERTIES LABELS "Bindings;JavaScript;Browser" TIMEOUT 90)
endforeach()

set_target_properties(Radient-GenerateJavaScript RadientWasm PROPERTIES FOLDER "DiligentFX/Bindings")
