"""Host-side checks for Android launchers, deployment, and failure cleanup."""

from pathlib import Path
import sys
import shlex
import subprocess
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import patch

import run_tests


class SDKLauncherTest(unittest.TestCase):
    def test_runner_uses_host_specific_launchers(self):
        for host, adb, d8 in [("win32", "adb.exe", "d8.bat"),
                              ("linux", "adb", "d8"), ("darwin", "adb", "d8")]:
            with self.subTest(host=host), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                sdk, build = root / "SDK", root / "build"
                tools = sdk / "build-tools/35.0.0"
                tools.mkdir(parents=True)
                (sdk / "platforms/android-35").mkdir(parents=True)
                (build / "DiligentFX/Radient/bindings/java/generated/classes").mkdir(parents=True)
                for name in ("libRadientJNI.so", "libRadientTest.so"):
                    (build / name).touch()
                with patch.object(run_tests.sys, "platform", host), \
                        patch.object(run_tests.sys, "argv", ["run_tests.py", "--sdk", str(sdk), "--build", str(build), "--results", str(root / "results")]), \
                        patch.object(run_tests, "run") as commands:
                    commands.return_value.returncode = 0
                    run_tests.main()
                launchers = [call.args[0] for call in commands.call_args_list]
                self.assertEqual(launchers[:3], ["javac", "jar", tools / d8])
                self.assertTrue(all(tool == sdk / "platform-tools" / adb for tool in launchers[3:]))

    @unittest.skipUnless(sys.platform == "win32", "Requires Windows batch execution")
    def test_batch_launcher_with_spaces(self):
        with tempfile.TemporaryDirectory(prefix="Android SDK ") as directory:
            root = Path(directory)
            launcher = run_tests.sdk_tool(root, "d8")
            output = root / "launch result.txt"
            launcher.write_text('@echo off\n> "%~1" echo %~2\n', encoding="utf-8")
            run_tests.run(launcher, output, "argument with spaces")
            self.assertEqual(output.read_text().strip(), "argument with spaces")


class DeviceRunnerTest(unittest.TestCase):
    def invoke(self, command):
        with patch.object(run_tests, "run", side_effect=command), \
                patch.object(run_tests, "find_library", side_effect=lambda build, name: build / name):
            run_tests.run_device_tests(["adb", "-s", "device"], Path("build"), Path("temporary"),
                                       "/data/local/tmp/test-run", Path("reports"), "Suite.*-Suite.Bad", 17)

    def test_deploys_assets_and_bounds_device_execution(self):
        calls = []

        def command(*args, **kwargs):
            calls.append((args, kwargs))
            return SimpleNamespace(returncode=0)

        self.invoke(command)
        self.assertTrue(any(args[-1] == "/data/local/tmp/test-run/assets" for args, _ in calls))
        execution, options = next((args, kwargs) for args, kwargs in calls
                                  if str(args[-1]).startswith("cd "))
        tokens = shlex.split(execution[-1])
        self.assertEqual(tokens[:3], ["cd", "/data/local/tmp/test-run/assets", "&&"])
        self.assertIn("TMPDIR=/data/local/tmp/test-run/tmp", tokens)
        self.assertIn("GTEST_FILTER=Suite.*-Suite.Bad", tokens)
        self.assertIn("GTEST_OUTPUT=xml:/data/local/tmp/test-run/results.xml", tokens)
        self.assertEqual(tokens[tokens.index("timeout") + 1], "17")
        self.assertGreater(options["timeout"], 17)
        self.assertEqual(calls[-2][0][3], "pull")
        self.assertEqual(calls[-1][0][-3:], ("rm", "-rf", "/data/local/tmp/test-run"))

    def test_test_failure_still_collects_report_and_cleans_device(self):
        calls = []
        failure = subprocess.CalledProcessError(1, "app_process")

        def command(*args, **kwargs):
            calls.append(args)
            if str(args[-1]).startswith("cd "):
                raise failure
            return SimpleNamespace(returncode=0)

        with self.assertRaises(subprocess.CalledProcessError) as caught:
            self.invoke(command)
        self.assertIs(caught.exception, failure)
        self.assertEqual(calls[-2][3], "pull")
        self.assertEqual(calls[-1][-3:], ("rm", "-rf", "/data/local/tmp/test-run"))

    def test_missing_report_is_not_success(self):
        def command(*args, **kwargs):
            return SimpleNamespace(returncode=1 if args[3] == "pull" else 0)

        with self.assertRaisesRegex(RuntimeError, "did not produce"):
            self.invoke(command)


if __name__ == "__main__":
    unittest.main()
