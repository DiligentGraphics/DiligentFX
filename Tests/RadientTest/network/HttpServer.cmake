# Configure within a function so optional HTTP library settings do not leak.
function(radient_add_test_http_server)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    set(HTTPLIB_INSTALL OFF)
    set(HTTPLIB_TEST OFF)
    set(HTTPLIB_COMPILE OFF)
    # The server binds to numeric loopback addresses and does not need async DNS.
    # Disabling it also avoids GetAddrInfoExCancel, absent from some MinGW headers.
    set(HTTPLIB_USE_NON_BLOCKING_GETADDRINFO OFF)
    set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF)
    set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF)
    set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF)
    set(HTTPLIB_USE_ZSTD_IF_AVAILABLE OFF)
    FetchContent_Declare(radient_httplib
        GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
        GIT_TAG adf58bf474fac638160592d6c3f67da4ebc7df20 # v0.28.0
    )
    FetchContent_MakeAvailable(radient_httplib)
    # Treat the vendored header as external code under /WX and -Werror.
    set_property(TARGET httplib APPEND PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${radient_httplib_SOURCE_DIR}")
endfunction()
radient_add_test_http_server()
