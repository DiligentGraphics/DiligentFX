# Runs the shared CPU and HTTP/GLTF fixtures inside Android's VM instead of a native
# executable, which has no JavaVM and cannot use the platform HTTP stack.
get_filename_component(FX_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
get_filename_component(CORE_SOURCE "${FX_SOURCE}/../DiligentCore" ABSOLUTE)
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
enable_testing()
add_test(NAME Radient-AndroidTestRunner
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/test_runner.py")
function(add_android_test_dependencies)
    if(NOT TARGET gtest)
        set(BUILD_GMOCK OFF)
        set(INSTALL_GTEST OFF)
        add_subdirectory("${CORE_SOURCE}/ThirdParty/googletest" "${CMAKE_CURRENT_BINARY_DIR}/googletest")
    endif()
    if(NOT TARGET Diligent-TestFramework)
        add_subdirectory("${CORE_SOURCE}/Tests/TestFramework" "${CMAKE_CURRENT_BINARY_DIR}/TestFramework")
    endif()
endfunction()
add_android_test_dependencies()

