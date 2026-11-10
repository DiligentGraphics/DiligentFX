set(RENDER_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(RENDER_MODEL "${CMAKE_CURRENT_BINARY_DIR}/RadientRender.json")
set(BROWSER_BINDINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../..")

add_custom_target(Radient-GenerateBrowserRenderTests
    COMMAND ${BINDING_COMMAND} "${CMAKE_CURRENT_SOURCE_DIR}/generate_model.py"
        --output "${RENDER_MODEL}" ${BINDING_ARGUMENTS}
    COMMAND ${BINDING_COMMAND} "${BINDING_TOOLS}/javascript/generate.py"
        --model "${RENDER_MODEL}" --module Radient
        --header Radient.h --header RadientMeshPrimitives.h --header NativeAdapters.hpp
        --header BrowserNative.h --header NativeCapture.h
        --adapters "${BROWSER_BINDINGS}/API.json" --output "${RENDER_GENERATED}"
    # Validate the production API even when only the render-test module is built.
    DEPENDS Radient-GenerateJavaScript
    BYPRODUCTS "${RENDER_GENERATED}/Radient.cpp" "${RENDER_GENERATED}/Radient.mjs"
        "${RENDER_GENERATED}/Radient.d.mts" "${RENDER_GENERATED}/runtime.mjs" "${RENDER_GENERATED}/byte_access.mjs"
    VERBATIM)

# Reuse the production bindings and library; only image utilities are test-only.
add_executable(RadientBrowserRenderTest EXCLUDE_FROM_ALL
    "${RENDER_GENERATED}/Radient.cpp" "${BROWSER_BINDINGS}/BrowserNative.cpp" NativeCapture.cpp)
add_dependencies(RadientBrowserRenderTest Radient-GenerateBrowserRenderTests)
configure_radient_wasm_target(RadientBrowserRenderTest "${RENDER_GENERATED}")
target_link_libraries(RadientBrowserRenderTest PRIVATE Diligent-TextureLoader Diligent-GraphicsTools)
target_include_directories(RadientBrowserRenderTest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(RadientBrowserRenderTest PROPERTIES OUTPUT_NAME RadientWasm)

add_test(NAME Radient-Browser-GoldenImages
    COMMAND "${RADIENT_BINDINGS_PYTHON_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/run_render_tests.py"
        --module "${RENDER_GENERATED}/Radient.mjs")
set_tests_properties(Radient-Browser-GoldenImages PROPERTIES LABELS "Bindings;Browser;WebGPU;GoldenImages" TIMEOUT 7200)
add_test(NAME Radient-Browser-ImageComparison
    COMMAND "${RADIENT_BINDINGS_PYTHON_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/test_images.py"
        --node "${RADIENT_BINDINGS_NODE_EXECUTABLE}" --module "${RENDER_GENERATED}/Radient.mjs")
set_tests_properties(Radient-Browser-ImageComparison PROPERTIES LABELS "Bindings;JavaScript" TIMEOUT 90)
set_target_properties(Radient-GenerateBrowserRenderTests RadientBrowserRenderTest PROPERTIES FOLDER "DiligentFX/Bindings")
