cmake_minimum_required(VERSION 3.24)

# Standalone JNI conformance tests: no renderer, Radient library, or browser.
project(DiligentJavaBindings LANGUAGES CXX)
enable_testing()

if(CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "The Java conformance project must use the host JVM architecture.")
endif()
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
find_package(Java 11 REQUIRED COMPONENTS Development Runtime)
find_package(JNI REQUIRED COMPONENTS JVM)
set(BINDINGS_PYTHON_EXECUTABLE "${Python3_EXECUTABLE}" CACHE FILEPATH "Python interpreter with Clang 22 bindings")
set(BINDINGS_LIBCLANG "" CACHE FILEPATH "LLVM 22 libclang library")
get_filename_component(BINDING_TOOLS "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
get_filename_component(CORE_SOURCE "${BINDING_TOOLS}/../../../DiligentCore" ABSOLUTE)
set(TEST_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
set(NATIVE_FIXTURES "${BINDING_TOOLS}/tests/native")
set(GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(MODEL "${GENERATED}/BindingTests.json")
set(PYTHON_COMMAND "${BINDINGS_PYTHON_EXECUTABLE}" -B)
if(BINDINGS_LIBCLANG)
    set(PYTHON_COMMAND "${CMAKE_COMMAND}" -E env "LIBCLANG_LIBRARY_FILE=${BINDINGS_LIBCLANG}" ${PYTHON_COMMAND})
endif()

if(MSVC)
    if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "ARM64")
        set(BINDING_TARGET aarch64-pc-windows-msvc)
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(BINDING_TARGET i686-pc-windows-msvc)
    else()
        set(BINDING_TARGET x86_64-pc-windows-msvc)
    endif()
elseif(CMAKE_CXX_COMPILER_TARGET)
    set(BINDING_TARGET "${CMAKE_CXX_COMPILER_TARGET}")
else()
    execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -dumpmachine
        OUTPUT_VARIABLE BINDING_TARGET OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
endif()
if(WIN32)
    set(PLATFORM_DEFINITION PLATFORM_WIN32=1)
elseif(APPLE)
    set(PLATFORM_DEFINITION PLATFORM_MACOS=1)
else()
    set(PLATFORM_DEFINITION PLATFORM_LINUX=1)
endif()
set(PARSER_ARGUMENTS "--target=${BINDING_TARGET}"
    "--clang-arg=-I${NATIVE_FIXTURES}"
    "--clang-arg=-I${CORE_SOURCE}/Primitives/interface" "--clang-arg=-D${PLATFORM_DEFINITION}")
foreach(INCLUDE_DIR IN LISTS CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES)
    list(APPEND PARSER_ARGUMENTS "--clang-arg=-isystem${INCLUDE_DIR}")
endforeach()
if(CMAKE_SYSROOT)
    list(APPEND PARSER_ARGUMENTS "--clang-arg=--sysroot=${CMAKE_SYSROOT}")
endif()

add_custom_target(Bindings-GenerateJavaTests
    COMMAND ${PYTHON_COMMAND} "${BINDING_TOOLS}/generate.py"
        --source-root "${BINDING_TOOLS}" --config "${TEST_SOURCE}/API.json"
        --output "${MODEL}" ${PARSER_ARGUMENTS}
    COMMAND ${PYTHON_COMMAND} "${CMAKE_CURRENT_SOURCE_DIR}/generate.py"
        --model "${MODEL}" --module BindingTests --package diligent.bindings.tests
        --library BindingTestsJNI --header BindingTestAPI.h --header JavaNames.h
        --header NativeExceptions.h --header Threading.h --output "${GENERATED}"
    BYPRODUCTS "${MODEL}" "${GENERATED}/BindingTests.java" "${GENERATED}/BindingTests.cpp"
    COMMENT "Generating Java/JNI binding test API"
    VERBATIM)

file(GLOB FIXTURE_SOURCES CONFIGURE_DEPENDS "${NATIVE_FIXTURES}/*.cpp" "${TEST_SOURCE}/fixtures/*.cpp")
add_library(BindingTestsJNI SHARED "${GENERATED}/BindingTests.cpp" ${FIXTURE_SOURCES})
add_dependencies(BindingTestsJNI Bindings-GenerateJavaTests)
target_include_directories(BindingTestsJNI PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${TEST_SOURCE}/fixtures" "${NATIVE_FIXTURES}"
    "${CORE_SOURCE}/Primitives/interface" ${JNI_INCLUDE_DIRS})
target_compile_features(BindingTestsJNI PRIVATE cxx_std_17)
target_compile_definitions(BindingTestsJNI PRIVATE ${PLATFORM_DEFINITION})
if(MSVC)
    target_compile_options(BindingTestsJNI PRIVATE /W4 /WX /EHsc)
else()
    target_compile_options(BindingTestsJNI PRIVATE -Wall -Wextra -Werror)
endif()

file(GLOB JAVA_SOURCES CONFIGURE_DEPENDS "${TEST_SOURCE}/*.java")
add_custom_command(OUTPUT "${GENERATED}/classes.stamp"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${GENERATED}/classes"
    COMMAND "${Java_JAVAC_EXECUTABLE}" --release 11 -encoding UTF-8 -Xlint:all -Werror
        -d "${GENERATED}/classes" "${GENERATED}/BindingTests.java" ${JAVA_SOURCES}
    COMMAND "${CMAKE_COMMAND}" -E touch "${GENERATED}/classes.stamp"
    DEPENDS "${GENERATED}/BindingTests.java" ${JAVA_SOURCES}
    COMMENT "Compiling Java marshalling tests"
    VERBATIM)
add_custom_target(Bindings-JavaClasses ALL DEPENDS "${GENERATED}/classes.stamp")
add_dependencies(Bindings-JavaClasses Bindings-GenerateJavaTests)

# CTest executes already-built tests. -Xcheck:jni also validates JNI usage.
file(GLOB JAVA_TESTS CONFIGURE_DEPENDS "${TEST_SOURCE}/*Test.java")
foreach(TEST_FILE IN LISTS JAVA_TESTS)
    get_filename_component(TEST_NAME "${TEST_FILE}" NAME_WE)
    add_test(NAME "Bindings-Java-${TEST_NAME}"
        COMMAND "${Java_JAVA_EXECUTABLE}" -Xcheck:jni
            "-Djava.library.path=$<TARGET_FILE_DIR:BindingTestsJNI>"
            -cp "${GENERATED}/classes" "diligent.bindings.tests.${TEST_NAME}")
    set_tests_properties("Bindings-Java-${TEST_NAME}" PROPERTIES LABELS "Bindings;Java" TIMEOUT 60)
endforeach()
file(GLOB PYTHON_TESTS CONFIGURE_DEPENDS "${TEST_SOURCE}/test_*.py")
foreach(TEST_FILE IN LISTS PYTHON_TESTS)
    get_filename_component(TEST_NAME "${TEST_FILE}" NAME_WE)
    add_test(NAME "Bindings-Java-${TEST_NAME}"
        COMMAND ${PYTHON_COMMAND} "${TEST_FILE}" "${MODEL}" -v)
    set_tests_properties("Bindings-Java-${TEST_NAME}" PROPERTIES LABELS "Bindings;Java")
endforeach()
