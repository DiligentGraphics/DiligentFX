#!/usr/bin/env python3
"""Run the shared resolver fixtures and generated bindings on an Android device."""

import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def run(*args):
    subprocess.run([str(arg) for arg in args], check=True)


def sdk_tool(directory, name):
    """SDK script launchers have different names on Windows."""
    suffix = (".bat" if name == "d8" else ".exe") if sys.platform == "win32" else ""
    return directory / (name + suffix)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True,
                        help="Root of the configured DiligentEngine Android build")
    parser.add_argument("--serial", help="adb device serial")
    args = parser.parse_args()
    adb = [sdk_tool(args.sdk / "platform-tools", "adb")]
    if args.serial:
        adb += ["-s", args.serial]
    build_tools = max((args.sdk / "build-tools").iterdir(),
                      key=lambda path: tuple(int(part) for part in path.name.split(".")))
    platform = max((args.sdk / "platforms").glob("android-*"),
                   key=lambda path: int(path.name.split("-")[1]))
    generated = args.build / "DiligentFX/Radient/bindings/java/generated"
    destination = "/data/local/tmp/radient-network-tests"
    with tempfile.TemporaryDirectory(prefix="radient-android-tests-") as temporary:
        temp = Path(temporary)
        classes = temp / "classes"
        shutil.copytree(generated / "classes", classes)
        run("javac", "--release", "11", "-Xlint:all", "-Werror", "-cp", classes,
            "-d", classes, Path(__file__).with_name("AndroidNetworkTest.java"))
        run("jar", "--create", "--file", temp / "tests.jar", "-C", classes, ".")
        run(sdk_tool(build_tools, "d8"), "--min-api", "33", "--lib", platform / "android.jar",
            "--output", temp, temp / "tests.jar")
        run(*adb, "shell", "mkdir", "-p", destination)
        run(*adb, "push", temp / "classes.dex", destination + "/classes.dex")
        for name in ("libRadientJNI.so", "libRadientNetworkAndroidTest.so"):
            matches = list(args.build.rglob(name))
            if len(matches) != 1:
                raise RuntimeError(f"Expected one {name} in {args.build}, found {matches}")
            run(*adb, "push", matches[0], destination + "/" + name)
        # app_process provides a real Android VM and platform networking without
        # introducing a separate Gradle application solely for native fixtures.
        run(*adb, "shell", f"CLASSPATH={destination}/classes.dex", "app_process",
            "-Xcheck:jni", f"-Djava.library.path={destination}", "/system/bin",
            "diligent.radient.tests.AndroidNetworkTest",
            destination + "/libRadientNetworkAndroidTest.so")


if __name__ == "__main__":
    main()
