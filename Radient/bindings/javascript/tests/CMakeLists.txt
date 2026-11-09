find_program(RADIENT_BINDINGS_NODE_EXECUTABLE NAMES node REQUIRED)

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

# Browser GPU bootstrap is separate from the suites that also run in Node.
if(WEBGPU_SUPPORTED)
    add_test(NAME Radient-JavaScript-Browser-WebGPU
        COMMAND ${BINDING_COMMAND} "${BINDING_TOOLS}/javascript/tests/run_browser_tests.py"
            --module "${GENERATED}/Radient.mjs"
            --suite "${CMAKE_CURRENT_SOURCE_DIR}/browser/bootstrap_tests.mjs"
            --shared-memory --timeout 180)
    set_tests_properties(Radient-JavaScript-Browser-WebGPU
        PROPERTIES LABELS "Bindings;JavaScript;Browser;WebGPU" TIMEOUT 210)
    add_subdirectory(browser/render)
endif()
