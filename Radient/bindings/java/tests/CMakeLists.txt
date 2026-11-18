file(GLOB JAVA_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.java")
set(TEST_CLASSES "${CMAKE_CURRENT_BINARY_DIR}/classes")
if(WIN32)
    set(TEST_CLASSPATH "${TEST_CLASSES};${RADIENT_JAVA_CLASSES}")
else()
    set(TEST_CLASSPATH "${TEST_CLASSES}:${RADIENT_JAVA_CLASSES}")
endif()

add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/classes.stamp"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${TEST_CLASSES}"
    COMMAND "${Java_JAVAC_EXECUTABLE}" --release 11 -encoding UTF-8 -Xlint:all -Werror
        -cp "${RADIENT_JAVA_CLASSES}" -d "${TEST_CLASSES}" ${JAVA_SOURCES}
    COMMAND "${CMAKE_COMMAND}" -E touch "${CMAKE_CURRENT_BINARY_DIR}/classes.stamp"
    DEPENDS "${GENERATED}/classes.stamp" ${JAVA_SOURCES}
    COMMENT "Compiling Radient Java integration tests"
    VERBATIM)
add_custom_target(Radient-JavaTests ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/classes.stamp")
add_dependencies(Radient-JavaTests Radient-JavaClasses RadientJNI)
set_target_properties(Radient-JavaTests PROPERTIES FOLDER "DiligentFX/Bindings")

# Tests run the already-built JNI library and classes, without creating a GPU.
file(GLOB JAVA_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*Test.java")
foreach(TEST_FILE IN LISTS JAVA_TESTS)
    get_filename_component(TEST_NAME "${TEST_FILE}" NAME_WE)
    add_test(NAME "Radient-Java-${TEST_NAME}"
        COMMAND "${Java_JAVA_EXECUTABLE}" -Xcheck:jni
            "-Djava.library.path=$<TARGET_FILE_DIR:RadientJNI>"
            "-Dradient.test.fixtures=${CMAKE_CURRENT_SOURCE_DIR}/../../tests/integration/fixtures.properties"
            -cp "${TEST_CLASSPATH}" "diligent.radient.tests.${TEST_NAME}")
    set_tests_properties("Radient-Java-${TEST_NAME}"
        PROPERTIES LABELS "Radient;Bindings;Java" TIMEOUT 60)
endforeach()
