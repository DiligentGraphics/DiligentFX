# Android Radient CPU tests

These tests run the shared `RadientTest` CPU suite and HTTP/glTF resolver fixtures
inside a real Android VM, plus a generated Java factory/engine integration check. They
exercise JNI thread attachment and exception cleanup, redirects, timeouts,
compressed responses, and imported external buffers and textures. A raw TCP
fixture stalls TLS negotiation to verify Android's documented use of the request
budget for the handshake, rather than `ConnectTimeoutMs`.

Configure an Android build of DiligentEngine with an NDK toolchain and:

```text
-DDILIGENT_BUILD_FX_TESTS=ON
-DDILIGENT_BUILD_GOOGLE_TEST=OFF
-DRADIENT_BUILD_JAVA_BINDINGS=ON
```

The Android test directory adds Google Test and the CPU testing framework itself;
the Core GPU test framework is not supported on Android. Supply the usual LLVM
22 binding generator settings. Build `RadientTest`, `RadientJNI`,
and `Radient-JavaClasses`.

With a booted emulator or connected device, run from a host with the Android SDK
build tools and a JDK on PATH:

```sh
python3 DiligentFX/Tests/RadientTest/android/run_tests.py \
    --sdk "$ANDROID_HOME" --build /path/to/android-build
```

Use `--serial` when more than one device is connected. The runner compiles the
Java entry point, converts classes with D8, and runs them through `app_process`
with JNI checking enabled. All CPU tests are selected by default; `--filter` accepts
Google Test filters. Test assets are deployed with the libraries, and each run has
its own writable working and temporary directories. Google Test output appears
in the console; `--results` selects the host report directory (default
`RadientAndroidTestResults`). The runner retrieves `results.xml` even when tests
fail and removes its device files afterward. `--timeout` bounds execution on the
device (default 600 seconds), including if the host loses its adb connection.
Windows uses the SDK's `d8.bat` and `adb.exe` launchers.
Host-side launcher checks are registered with CTest as `Radient-AndroidTestRunner`
and can also be run directly with `python test_runner.py`.
The generated Java wrappers use `java.lang.ref.Cleaner`,
so this integration test requires Android API 33 or newer. The native resolver
does not depend on Cleaner.

This harness uses the shell process's network permission. Applications must
declare `android.permission.INTERNET` and configure any required cleartext-network
exceptions themselves. The loopback tests do not validate application manifests
or HTTPS certificate policies.
