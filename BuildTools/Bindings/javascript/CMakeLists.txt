cmake_minimum_required(VERSION 3.20)

# A standalone conformance project; does not configure Radient or a renderer.
project(DiligentJavaScriptBindings LANGUAGES C CXX)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "Configure with the Emscripten CMake toolchain to build the Wasm binding tests.")
endif()

find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
find_program(BINDINGS_NODE_EXECUTABLE NAMES node REQUIRED)
set(BINDINGS_PYTHON_EXECUTABLE "${Python3_EXECUTABLE}" CACHE FILEPATH "Python interpreter with Clang 22 bindings")
set(BINDINGS_LIBCLANG "" CACHE FILEPATH "LLVM 22 libclang library")

get_filename_component(BINDING_TOOLS "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
get_filename_component(CORE_SOURCE "${BINDING_TOOLS}/../../../DiligentCore" ABSOLUTE)
set(TEST_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
set(GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(MODEL "${GENERATED}/BindingTests.json")
set(PYTHON_COMMAND "${BINDINGS_PYTHON_EXECUTABLE}" -B)
if(BINDINGS_LIBCLANG)
    set(PYTHON_COMMAND "${CMAKE_COMMAND}" -E env "LIBCLANG_LIBRARY_FILE=${BINDINGS_LIBCLANG}" ${PYTHON_COMMAND})
endif()

# Extract against the same Wasm SDK as the compiler. Native model targets remain
# independent of this project and do not require an Emscripten installation.
set(PARSER_ARGUMENTS --target=wasm32-unknown-emscripten
    "--clang-arg=-I${CORE_SOURCE}/Primitives/interface"
    "--clang-arg=--sysroot=${EMSCRIPTEN_SYSROOT}"
    "--clang-arg=-isystem${EMSCRIPTEN_SYSROOT}/include/c++/v1"
    --clang-arg=-DPLATFORM_WEB=1)

# Run the small extraction on demand so transitive public-header edits are picked
# up. Generators leave unchanged files untouched, avoiding needless recompilation.
add_custom_target(Bindings-GenerateJavaScriptTests
    COMMAND ${PYTHON_COMMAND} "${BINDING_TOOLS}/generate.py"
        --source-root "${TEST_SOURCE}" --config "${TEST_SOURCE}/API.json"
        --output "${MODEL}" ${PARSER_ARGUMENTS}
    COMMAND ${PYTHON_COMMAND} "${CMAKE_CURRENT_SOURCE_DIR}/generate.py"
        --model "${MODEL}" --module BindingTests --header Interfaces.h --output "${GENERATED}"
    BYPRODUCTS "${MODEL}" "${GENERATED}/BindingTests.cpp" "${GENERATED}/BindingTests.mjs"
        "${GENERATED}/BindingTests.d.mts" "${GENERATED}/runtime.mjs"
    COMMENT "Generating JavaScript binding test API"
    VERBATIM)

add_executable(BindingTestsWasm EXCLUDE_FROM_ALL
    "${GENERATED}/BindingTests.cpp"
    "${TEST_SOURCE}/fixtures/Implementation.cpp"
    "${TEST_SOURCE}/fixtures/IncludeTest.c")
add_dependencies(BindingTestsWasm Bindings-GenerateJavaScriptTests)
target_include_directories(BindingTestsWasm PRIVATE "${TEST_SOURCE}/fixtures" "${CORE_SOURCE}/Primitives/interface")
target_compile_features(BindingTestsWasm PRIVATE cxx_std_17)
target_compile_options(BindingTestsWasm PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(BindingTestsWasm PRIVATE PLATFORM_WEB=1)
target_link_options(BindingTestsWasm PRIVATE
    --no-entry -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,node
    -sALLOW_MEMORY_GROWTH=1 -sASSERTIONS=1
    "-sEXPORTED_FUNCTIONS=['_malloc','_free']"
    "-sEXPORTED_RUNTIME_METHODS=['HEAPU8']")
set_target_properties(BindingTestsWasm PROPERTIES SUFFIX ".mjs" RUNTIME_OUTPUT_DIRECTORY "${GENERATED}")

add_custom_target(Bindings-TestJavaScript
    COMMAND ${PYTHON_COMMAND} "${TEST_SOURCE}/test_generator.py" "${MODEL}"
    COMMAND "${BINDINGS_NODE_EXECUTABLE}" "${TEST_SOURCE}/test_marshalling.mjs" "${GENERATED}/BindingTests.mjs"
    DEPENDS BindingTestsWasm
    COMMENT "Testing generated bindings through JavaScript, Wasm, and native test objects"
    VERBATIM)
