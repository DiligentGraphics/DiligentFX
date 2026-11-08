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

add_custom_target(Radient-TestJavaScript
    COMMAND ${BINDING_COMMAND} "${CMAKE_CURRENT_SOURCE_DIR}/test_generation.py" "${MODEL}"
    COMMAND "${RADIENT_BINDINGS_NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_data_blob.mjs" "${GENERATED}/Radient.mjs"
    DEPENDS RadientWasm
    COMMENT "Testing generated JavaScript bindings against Radient data blobs"
    VERBATIM)
set_target_properties(Radient-GenerateJavaScript RadientWasm Radient-TestJavaScript
    PROPERTIES FOLDER "DiligentFX/Bindings")
