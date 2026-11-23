# Building Radient

Radient is built as part of DiligentEngine's CMake build and is enabled unless
`DILIGENT_NO_RADIENT` is set. These notes supplement the normal engine build
instructions with Radient-specific prerequisites.

## Native prerequisites

On native platforms other than desktop Windows and Apple, including UWP, install OpenSSL
development headers and libraries (for example, `libssl-dev` on Ubuntu). Set
`OPENSSL_ROOT_DIR` if CMake cannot find them automatically. Cross builds require OpenSSL libraries built for
the target platform, rather than the host. HTTPS also requires an appropriate
CA trust store at runtime.

Desktop Windows uses the platform TLS implementation and does not require
OpenSSL. UWP requires OpenSSL built for Windows Store; desktop Windows libraries
cannot be used for that target.
Apple platforms use Foundation's NSURLSession for HTTP(S), TLS, and certificate
verification; curl and OpenSSL are not required. Applications must allow network
access through their sandbox entitlements and App Transport Security configuration.
The resolver does not override those policies.
Web builds do not require native HTTP or TLS libraries. The browser network
transport is not implemented yet in this branch.

## Offline configuration

CMake downloads source dependencies during configuration. On platforms using curl,
set `FETCHCONTENT_SOURCE_DIR_RADIENT_CURL` to supply an extracted source tree
matching the release pinned in [RadientCurl.cmake](../cmake/RadientCurl.cmake).
Installed libcurl binaries are not used.

Native test builds, except on 32-bit Windows, also require the HTTP test-server
source. It can be supplied through `FETCHCONTENT_SOURCE_DIR_RADIENT_HTTPLIB`,
matching the revision in
[HttpServer.cmake](../../Tests/RadientTest/network/HttpServer.cmake). The native
resolver tests start their own loopback server; no external service is required.
The HTTP-server tests are omitted on 32-bit Windows because cpp-httplib does not
support that platform. The production network resolver remains enabled.

These overrides cover the resolver dependencies only. Other engine dependencies
must also be available locally for a completely offline build. Existing downloads
are reused within a build directory.
