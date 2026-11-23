"""Host-side regression checks for Android SDK launcher selection."""

from pathlib import Path
import sys
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
                for name in ("libRadientJNI.so", "libRadientNetworkAndroidTest.so"):
                    (build / name).touch()
                with patch.object(run_tests.sys, "platform", host), \
                        patch.object(run_tests.sys, "argv", ["run_tests.py", "--sdk", str(sdk), "--build", str(build)]), \
                        patch.object(run_tests, "run") as commands:
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


if __name__ == "__main__":
    unittest.main()
