cmake_minimum_required (VERSION 3.10)

# Reuse Core's browser harness for the native Radient test suites.
function(configure_radient_browser_test TARGET)
    if(PLATFORM_WEB)
        # Keep invoke_* wrappers synchronous during static initialization, before
        # main() enters through WebAssembly.promising and can suspend for GPU waits.
        target_link_options(${TARGET} PRIVATE
            "-pthread"
            "-sJSPI=1"
            "-sASYNCIFY_IGNORE_INDIRECT=1"
            "-sALLOW_MEMORY_GROWTH=1"
            "-sPTHREAD_POOL_SIZE=Math.max(1,navigator.hardwareConcurrency)*4"
            "-sSTACK_SIZE=8388608"
            "-sEXIT_RUNTIME=1"
            "-sEXPORTED_RUNTIME_METHODS=FS"
            "SHELL: --preload-file '${CMAKE_CURRENT_SOURCE_DIR}/assets@/'"
        )
    endif()
endfunction()

if(PLATFORM_ANDROID AND DILIGENT_BUILD_FX_TESTS)
    # Android network tests need a JVM, but not Core's desktop GPU test framework.
    add_subdirectory(RadientTest/network/android)
elseif(TARGET gtest)
    if(DILIGENT_BUILD_FX_TESTS)
        add_subdirectory(DiligentFXGPUTest)
        add_subdirectory(RadientTest)
        add_subdirectory(RadientGPUTest)
        add_subdirectory(RadientRenderTest)
    endif()
endif()

if(DILIGENT_BUILD_FX_INCLUDE_TEST)
    add_subdirectory(IncludeTest)
endif()
