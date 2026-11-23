# Android network resolver tests

These tests run the existing native HTTP and glTF resolver fixtures inside a
real Android VM, plus a generated Java factory/engine integration check. They
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
22 binding generator settings. Build `RadientNetworkAndroidTest`, `RadientJNI`,
and `Radient-JavaClasses`.

With a booted emulator or connected device, run from a host with the Android SDK
build tools and a JDK on PATH:

```sh
python3 DiligentFX/Tests/RadientTest/network/android/run_tests.py \
    --sdk "$ANDROID_HOME" --build /path/to/android-build
```

Use `--serial` when more than one device is connected. The runner compiles the
Java entry point, converts classes with D8, and runs them through `app_process`
with JNI checking enabled. Windows uses the SDK's `d8.bat` and `adb.exe` launchers.
Host-side launcher checks are registered with CTest as `Radient-AndroidTestRunner`
and can also be run directly with `python test_runner.py`.
The generated Java wrappers use `java.lang.ref.Cleaner`,
so this integration test requires Android API 33 or newer. The native resolver
does not depend on Cleaner.

This harness uses the shell process's network permission. Applications must
declare `android.permission.INTERNET` and configure any required cleartext-network
exceptions themselves. The loopback tests do not validate application manifests
or HTTPS certificate policies.
