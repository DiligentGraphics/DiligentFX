cmake_minimum_required(VERSION 3.20)

# Configured through the Radient target in an Emscripten build.
find_program(RADIENT_BINDINGS_NODE_EXECUTABLE NAMES node REQUIRED)
set(MODEL "${CMAKE_CURRENT_BINARY_DIR}/../generated/RadientDataBlob.json")
set(GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated")

add_custom_target(Radient-GenerateJavaScript
    COMMAND ${BINDING_COMMAND} "${BINDING_TOOLS}/javascript/generate.py"
        --model "${MODEL}" --module Radient --header RadientDataBlob.h
        --adapters "${CMAKE_CURRENT_SOURCE_DIR}/DataBlob.json" --output "${GENERATED}"
    DEPENDS Radient-GenerateBindingModel
    BYPRODUCTS "${GENERATED}/Radient.cpp" "${GENERATED}/Radient.mjs"
        "${GENERATED}/Radient.d.mts" "${GENERATED}/runtime.mjs" "${GENERATED}/byte_access.mjs"
    COMMENT "Generating Radient JavaScript data-blob bindings"
    VERBATIM)

add_executable(RadientWasm EXCLUDE_FROM_ALL "${GENERATED}/Radient.cpp")
add_dependencies(RadientWasm Radient-GenerateJavaScript)
set_common_target_properties(RadientWasm)
target_link_libraries(RadientWasm PRIVATE Radient Diligent-BuildSettings)
target_link_options(RadientWasm PRIVATE
    -pthread --no-entry -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,node
    -sALLOW_MEMORY_GROWTH=1 -sASSERTIONS=1
    "-sEXPORTED_FUNCTIONS=['_malloc','_free']"
    "-sEXPORTED_RUNTIME_METHODS=['HEAPU8']")
set_target_properties(RadientWasm PROPERTIES SUFFIX ".mjs" RUNTIME_OUTPUT_DIRECTORY "${GENERATED}")

# Build RadientWasm first; these tests never generate or rebuild artifacts.
add_test(NAME Radient-JavaScriptGeneration
    COMMAND ${BINDING_COMMAND} "${CMAKE_CURRENT_SOURCE_DIR}/test_generation.py" "${MODEL}" -v)
add_test(NAME Radient-JavaScriptDataBlob
    COMMAND "${RADIENT_BINDINGS_NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_data_blob.mjs" "${GENERATED}/Radient.mjs")
set_tests_properties(Radient-JavaScriptGeneration Radient-JavaScriptDataBlob
    PROPERTIES LABELS "Bindings;JavaScript")

set(RADIENT_BINDINGS_BROWSER_EXECUTABLE "" CACHE FILEPATH "Chrome, Chromium, or Edge for browser tests (empty auto-detects)")
set(BROWSER_ARGUMENTS)
if(RADIENT_BINDINGS_BROWSER_EXECUTABLE)
    list(APPEND BROWSER_ARGUMENTS --browser "${RADIENT_BINDINGS_BROWSER_EXECUTABLE}")
endif()

add_test(NAME Radient-JavaScriptBrowser
    COMMAND ${BINDING_COMMAND} "${BINDING_TOOLS}/javascript/tests/run_browser_tests.py"
        --module "${GENERATED}/Radient.mjs"
        --suite "${CMAKE_CURRENT_SOURCE_DIR}/data_blob_tests.mjs"
        --shared-memory ${BROWSER_ARGUMENTS})
set_tests_properties(Radient-JavaScriptBrowser PROPERTIES LABELS "Bindings;JavaScript;Browser")

set_target_properties(Radient-GenerateJavaScript RadientWasm PROPERTIES FOLDER "DiligentFX/Bindings")
