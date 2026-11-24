#!/usr/bin/env python3
"""Run the shared Radient CPU suite and Java integration checks on Android."""

import argparse
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import uuid


def run(*args, check=True, timeout=120):
    return subprocess.run([str(arg) for arg in args], check=check, timeout=timeout)


def try_run(*args):
    """Collect diagnostics and clean up without hiding the original test failure."""
    try:
        return run(*args, check=False).returncode == 0
    except (OSError, subprocess.SubprocessError) as error:
        print(f"Android test cleanup: {error}", file=sys.stderr)
        return False


def sdk_tool(directory, name):
    """SDK script launchers have different names on Windows."""
    suffix = (".bat" if name == "d8" else ".exe") if sys.platform == "win32" else ""
    return directory / (name + suffix)


def compile_launcher(sdk, generated, temporary, compile_sdk, build_tools_version):
    # Use the requested packages exactly; newer installed SDKs must not change
    # the launcher or D8 version used by an otherwise identical test run.
    d8 = sdk_tool(sdk / "build-tools" / build_tools_version, "d8")
    android_jar = sdk / "platforms" / ("android-" + compile_sdk) / "android.jar"
    for path in (d8, android_jar):
        if not path.is_file():
            raise RuntimeError(f"Required Android SDK file is missing: {path}. "
                               f'Install "platforms;android-{compile_sdk}" and '
                               f'"build-tools;{build_tools_version}" with sdkmanager.')
    print(f"Android launcher: compile SDK {compile_sdk}, build tools {build_tools_version}", flush=True)
    classes = temporary / "classes"
    shutil.copytree(generated / "classes", classes)
    run("javac", "--release", "11", "-Xlint:all", "-Werror", "-cp", classes,
        "-d", classes, Path(__file__).with_name("AndroidTest.java"))
    run("jar", "--create", "--file", temporary / "tests.jar", "-C", classes, ".")
    run(d8, "--min-api", "33", "--lib", android_jar,
        "--output", temporary, temporary / "tests.jar")


def find_library(build, name):
    matches = list(build.rglob(name))
    if len(matches) != 1:
        raise RuntimeError(f"Expected one {name} in {build}, found {matches}")
    return matches[0]


def run_device_tests(adb, build, temporary, destination, results, test_filter, timeout):
    try:
        run(*adb, "shell", "mkdir", "-p", destination + "/tmp")
        run(*adb, "push", temporary / "classes.dex", destination + "/classes.dex")
        for name in ("libRadientJNI.so", "libRadientTest.so"):
            run(*adb, "push", find_library(build, name), destination + "/" + name)
        run(*adb, "push", Path(__file__).resolve().parent.parent / "assets", destination + "/assets")
        # app_process supplies the JVM required by Android networking. Tests use
        # ordinary files, so their working directory and temporary directory must
        # both be writable locations belonging to this invocation.
        command = ["env", f"TMPDIR={destination}/tmp", f"CLASSPATH={destination}/classes.dex",
                   f"GTEST_FILTER={test_filter}", f"GTEST_OUTPUT=xml:{destination}/results.xml",
                   "timeout", str(timeout), "app_process", "-Xcheck:jni", f"-Djava.library.path={destination}",
                   "/system/bin", "diligent.radient.tests.AndroidTest", destination + "/libRadientTest.so"]
        try:
            run(*adb, "shell", "cd " + shlex.quote(destination + "/assets") + " && " + shlex.join(command),
                timeout=timeout + 30)
        finally:
            # Preserve failed-test reports without masking a launcher/crash error.
            report = try_run(*adb, "pull", destination + "/results.xml", results / "results.xml")
        if not report:
            raise RuntimeError("Android tests did not produce their Google Test report")
    finally:
        try_run(*adb, "shell", "rm", "-rf", destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--compile-sdk", default="35", help="Exact Android SDK platform version (default: 35)")
    parser.add_argument("--build-tools-version", default="35.0.0", help="Exact Android build-tools version (default: 35.0.0)")
    parser.add_argument("--build", type=Path, required=True,
                        help="Root of the configured DiligentEngine Android build (one ABI)")
    parser.add_argument("--serial", help="adb device serial")
    parser.add_argument("--filter", default="*", help="Google Test filter")
    parser.add_argument("--results", type=Path, default=Path("RadientAndroidTestResults"))
    parser.add_argument("--timeout", type=int, default=600, help="Device test timeout in seconds")
    args = parser.parse_args()
    adb = [sdk_tool(args.sdk / "platform-tools", "adb")]
    if args.serial:
        adb += ["-s", args.serial]
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    args.results.mkdir(parents=True, exist_ok=True)
    (args.results / "results.xml").unlink(missing_ok=True)
    generated = args.build / "DiligentFX/Radient/bindings/java/generated"
    destination = "/data/local/tmp/radient-tests-" + uuid.uuid4().hex
    with tempfile.TemporaryDirectory(prefix="radient-android-tests-") as temporary:
        temp = Path(temporary)
        compile_launcher(args.sdk, generated, temp, args.compile_sdk, args.build_tools_version)
        run_device_tests(adb, args.build, temp, destination, args.results, args.filter, args.timeout)


if __name__ == "__main__":
    main()
