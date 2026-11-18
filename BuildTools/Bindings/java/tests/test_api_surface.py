"""Check API surfaces using real Java fixture declarations."""

from copy import deepcopy
import subprocess
import tempfile
import json
from pathlib import Path
import sys
import unittest

TOOLS = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(TOOLS / "tests"))
import api_surface_cases
from java.generate import Generator
from configuration import load_config

MODEL_PATH = Path(sys.argv.pop(1))


class APISurfaceTest(api_surface_cases.APISurfaceCases):
    model_data = json.loads(MODEL_PATH.read_text(encoding="utf-8"))
    adapters = load_config(TOOLS / "tests/native/Adapters.json")
    target = "native"

    def make_generator(self, model, adapters):
        return Generator(model, "BindingTests", "diligent.bindings.tests", "BindingTestsJNI", ["BindingTestAPI.h"], adapters)

    def test_native_integer_spelling_and_platform_names_are_normalized(self):
        model = deepcopy(self.model_data)
        model["target"] = "x86_64-unknown-linux-gnu"
        def lp64(item):
            if isinstance(item, dict):
                if item.get("kind") == "scalar" and item["name"] in ("longlong", "ulonglong"):
                    item["name"] = {"longlong": "long", "ulonglong": "ulong"}[item["name"]]
                for child in item.values():
                    lp64(child)
            elif isinstance(item, list):
                for child in item:
                    lp64(child)
        lp64(model)
        self.assertEqual(self.surface(model), self.surface())

    def test_native_type_width_and_signedness_changes_are_detected(self):
        for name, bits in (("uint", 32), ("longlong", 64)):
            with self.subTest(name=name, bits=bits):
                model = deepcopy(self.model_data)
                field = self.declaration(model, "MarshalEntityID")["type"]
                while field["kind"] == "alias":
                    field = field["target"]
                field.update(name=name, bits=bits)
                self.assertNotEqual(self.surface(model), self.surface())

    def test_generator_writes_surface_from_effective_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            surface = Path(directory) / "api_surface.json"
            command = [sys.executable, "-B", str(TOOLS / "java/generate.py"),
                       "--model", str(MODEL_PATH), "--module", "BindingTests",
                       "--package", "diligent.bindings.tests", "--library", "BindingTestsJNI",
                       "--header", "BindingTestAPI.h", "--output", directory,
                       "--adapters", str(TOOLS / "tests/native/Adapters.json"),
                       "--surface", str(surface)]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(surface.read_text(encoding="utf-8")), self.surface())


if __name__ == "__main__":
    unittest.main()
