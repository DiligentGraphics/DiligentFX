cmake_minimum_required(VERSION 3.24)

# Configured through the production Radient target with RADIENT_BUILD_JAVA_BINDINGS.
find_package(Java 11 REQUIRED COMPONENTS Development Runtime)
find_package(JNI REQUIRED COMPONENTS JVM)

set(GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(MODEL "${CMAKE_CURRENT_BINARY_DIR}/../generated/Radient.json")

add_custom_target(Radient-GenerateJava
    COMMAND ${BINDING_COMMAND} "${BINDING_TOOLS}/java/generate.py"
        --model "${MODEL}" --module Radient --package diligent.radient
        --library RadientJNI --header Radient.h --header RadientMeshPrimitives.h
        --header NativeAdapters.hpp --adapters "${CMAKE_CURRENT_SOURCE_DIR}/../Adapters.json"
        --output "${GENERATED}" --surface "${GENERATED}/api_surface.json"
    # Generation checks the reviewed contract before JNI or Java compilation.
    # Only the explicit Python update command may accept a changed baseline.
    COMMAND ${BINDING_COMMAND} "${BINDING_TOOLS}/api_surface.py"
        --actual "${GENERATED}/api_surface.json"
        --baseline "${CMAKE_CURRENT_SOURCE_DIR}/api_surface.json"
    BYPRODUCTS "${GENERATED}/Radient.java" "${GENERATED}/Radient.cpp"
        "${GENERATED}/api_surface.json"
    COMMENT "Generating Radient Java/JNI bindings"
    VERBATIM)
add_dependencies(Radient-GenerateJava Radient-GenerateBindingModel)

add_library(RadientJNI SHARED "${GENERATED}/Radient.cpp")
add_dependencies(RadientJNI Radient-GenerateJava)
set_common_target_properties(RadientJNI)
target_compile_features(RadientJNI PRIVATE cxx_std_17)
target_include_directories(RadientJNI PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.." "${BINDING_TOOLS}/java" ${JNI_INCLUDE_DIRS})
# JNI is loaded by an existing JVM; no dependency on libjvm is needed.
target_link_libraries(RadientJNI PRIVATE Radient Diligent-BuildSettings)

set(RADIENT_JAVA_CLASSES "${GENERATED}/classes")
add_custom_command(OUTPUT "${GENERATED}/classes.stamp"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${RADIENT_JAVA_CLASSES}"
    COMMAND "${Java_JAVAC_EXECUTABLE}" --release 11 -encoding UTF-8 -Xlint:all -Werror
        -d "${RADIENT_JAVA_CLASSES}" "${GENERATED}/Radient.java"
    COMMAND "${CMAKE_COMMAND}" -E touch "${GENERATED}/classes.stamp"
    DEPENDS "${GENERATED}/Radient.java"
    COMMENT "Compiling Radient Java bindings"
    VERBATIM)
add_custom_target(Radient-JavaClasses ALL DEPENDS "${GENERATED}/classes.stamp")
add_dependencies(Radient-JavaClasses Radient-GenerateJava)

add_subdirectory(tests)
set_target_properties(Radient-GenerateJava RadientJNI Radient-JavaClasses
    PROPERTIES FOLDER "DiligentFX/Bindings")
