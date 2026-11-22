# Build the same pinned libcurl source on every native platform.
# Keep dependency options local: BUILD_TESTING and BUILD_SHARED_LIBS also belong
# to the parent project and must not be overwritten in its cache.
function(radient_fetch_curl)
    include(FetchContent)
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()

    # curl also declares string-valued cache options. Preserve our local values
    # when those cache entries are first created (as option() does with CMP0077).
    set(CMAKE_POLICY_DEFAULT_CMP0126 NEW)
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_STATIC_LIBS ON)
    # Only a static library is built; compile its sources directly in that target.
    set(SHARE_LIB_OBJECT OFF)
    set(BUILD_CURL_EXE OFF)
    set(BUILD_TESTING OFF)
    set(BUILD_EXAMPLES OFF)
    set(BUILD_LIBCURL_DOCS OFF)
    set(BUILD_MISC_DOCS OFF)
    set(ENABLE_CURL_MANUAL OFF)
    set(CURL_DISABLE_INSTALL ON)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)

    # Only HTTP(S) is used. Threaded DNS lets the transfer time out without
    # c-ares or process-wide signals, but cleanup can still wait for OS lookups.
    foreach(protocol IN ITEMS DICT FILE FTP GOPHER IMAP IPFS LDAP LDAPS MQTT
                              POP3 RTSP SMTP TELNET TFTP WEBSOCKETS)
        set(CURL_DISABLE_${protocol} ON)
    endforeach()
    set(CURL_ENABLE_SSL ON)
    set(ENABLE_THREADED_RESOLVER ON)
    set(ENABLE_ARES OFF)
    set(CURL_USE_LIBPSL OFF)
    set(CURL_USE_LIBSSH2 OFF)
    set(USE_NGHTTP2 OFF)
    set(USE_LIBIDN2 OFF)

    # Radient validates and decodes compressed responses with its existing zlib.
    set(CURL_ZLIB OFF)
    set(CURL_BROTLI OFF)
    set(CURL_ZSTD OFF)
    # curl also consults inherited discovery results when these options are off.
    set(ZLIB_FOUND FALSE)
    set(BROTLI_FOUND FALSE)
    set(ZSTD_FOUND FALSE)

    # curl supports Schannel on desktop Windows, but not Windows Store (UWP).
    if(WIN32 AND NOT WINDOWS_STORE)
        set(CURL_USE_SCHANNEL ON)
        set(CURL_USE_OPENSSL OFF)
    else()
        set(CURL_USE_SCHANNEL OFF)
        set(CURL_USE_OPENSSL ON)
    endif()

    FetchContent_Declare(radient_curl
        URL https://curl.se/download/curl-8.23.0.tar.xz
        URL_HASH SHA256=8dadd7d2536a314f982ad05a651b713f426bf7d02cc2c76cffb9796f2218e6c5
    )
    FetchContent_MakeAvailable(radient_curl)
    set_target_properties(libcurl_static PROPERTIES FOLDER "DiligentFX/ThirdParty/curl")
endfunction()
