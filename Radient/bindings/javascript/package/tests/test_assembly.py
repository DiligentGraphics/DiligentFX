"""Packaging guards prevent stale/debug/incomplete artifacts entering a release."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location("assembly", Path(__file__).resolve().parents[1] / "assemble.py")
assembly = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(assembly)


class AssemblyTest(unittest.TestCase):
    def test_requires_release_webgpu(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            for configuration, webgpu_disabled, valid in [("Debug", "OFF", False), ("Release", "ON", False), ("Release", "OFF", True)]:
                (build / "CMakeCache.txt").write_text(f"CMAKE_BUILD_TYPE:STRING={configuration}\nDILIGENT_NO_WEBGPU:BOOL={webgpu_disabled}\nEMSCRIPTEN:INTERNAL=1\n")
                if valid:
                    self.assertEqual(assembly.read_cache(build)["CMAKE_BUILD_TYPE"], "Release")
                else:
                    with self.assertRaises(ValueError):
                        assembly.read_cache(build)

    def test_missing_runtime_does_not_create_package(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            (build / "CMakeCache.txt").write_text("CMAKE_BUILD_TYPE:STRING=Release\nDILIGENT_NO_WEBGPU:BOOL=OFF\nEMSCRIPTEN:INTERNAL=1\n")
            output = build / "package"
            with self.assertRaisesRegex(ValueError, "Missing Radient.mjs"):
                assembly.assemble(build, output, None)
            self.assertFalse(output.exists())

    def test_rejects_native_build(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            (build / "CMakeCache.txt").write_text("CMAKE_BUILD_TYPE:STRING=Release\nDILIGENT_NO_WEBGPU:BOOL=OFF\n")
            with self.assertRaisesRegex(ValueError, "Emscripten"):
                assembly.read_cache(build)

    def test_does_not_overwrite_existing_output(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            (build / "CMakeCache.txt").write_text("CMAKE_BUILD_TYPE:STRING=Release\nDILIGENT_NO_WEBGPU:BOOL=OFF\nEMSCRIPTEN:INTERNAL=1\n")
            output = build / "package"
            output.mkdir()
            sentinel = output / "old.wasm"
            sentinel.write_bytes(b"untouched")
            with self.assertRaisesRegex(ValueError, "must not already exist"):
                assembly.assemble(build, output, None)
            self.assertEqual(sentinel.read_bytes(), b"untouched")

    def test_dependency_notices_preserve_paths_and_skip_git(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            for name in ("LICENSE.txt", "nested/COPYRIGHT", "nested/stb_image_license.txt", ".git/LICENSE", "code.cpp", "check_copyright.py", "license-checker.cfg",
                         "restrictive_license_approval.textproto"):
                path = source / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(name)
            self.assertEqual(assembly.copy_licenses(source, root / "licenses"), 3)
            self.assertEqual((root / "licenses/nested/COPYRIGHT").read_text(), "nested/COPYRIGHT")
            self.assertFalse((root / "licenses/.git").exists())
            self.assertFalse((root / "licenses/check_copyright.py").exists())
            self.assertFalse((root / "licenses/license-checker.cfg").exists())


if __name__ == "__main__":
    unittest.main()
