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


def replace_scalar_names(item, replacements):
    if isinstance(item, dict):
        if item.get("kind") == "scalar":
            item["name"] = replacements.get(item["name"], item["name"])
        for child in item.values():
            replace_scalar_names(child, replacements)
    elif isinstance(item, list):
        for child in item:
            replace_scalar_names(child, replacements)


class APISurfaceTest(api_surface_cases.APISurfaceCases):
    model_data = json.loads(MODEL_PATH.read_text(encoding="utf-8"))
    adapters = load_config(TOOLS / "tests/native/Adapters.json")
    target = "native"

    def test_function_adapter_is_part_of_reviewed_contract(self):
        adapters = deepcopy(self.adapters)
        adapters["function_adapters"] = {"Diligent::EchoTestMode": "Adapters::EchoMode"}
        generator = self.make_generator(deepcopy(self.model_data), adapters)
        generator.generate()
        from api_surface import build_surface
        surface = build_surface(generator, target="native")
        self.assertEqual(surface["entries"]["Diligent::EchoTestMode"]["function_adapter"], "Adapters::EchoMode")

    def make_generator(self, model, adapters):
        return Generator(model, "BindingTests", "diligent.bindings.tests", "BindingTestsJNI", ["BindingTestAPI.h"], adapters)

    def test_native_integer_spelling_and_platform_names_are_normalized(self):
        model = deepcopy(self.model_data)
        model["target"] = "x86_64-unknown-linux-gnu"
        replace_scalar_names(model, {"longlong": "long", "ulonglong": "ulong"})
        self.assertEqual(self.surface(model), self.surface())

    def test_plain_char_signedness_is_platform_dependent(self):
        model = deepcopy(self.model_data)
        replace_scalar_names(model, {"char_s": "char_u"})
        actual, baseline = self.surface(model)["entries"], self.surface()["entries"]
        for name in ("TextByteCount", "GetEscapedExportName", "MarshalExportNames::Name",
                     "MarshalAnimationName", "MarshalAnimationNamePointer"):
            with self.subTest(name=name):
                self.assertEqual(actual["Diligent::" + name], baseline["Diligent::" + name])
        # Numeric char arrays produce different Java array types; preserve that
        # distinction for both fixed arrays and pointer/count descriptor fields.
        for name in ("MarshalFixedArrays::ValuesChar", "MarshalOutputArrayDesc::pChar"):
            with self.subTest(name=name):
                self.assertNotEqual(actual["Diligent::" + name], baseline["Diligent::" + name])

    def test_character_pointer_aliases_preserve_numeric_span_signedness(self):
        from api_surface import build_surface
        for const in (True, False):
            with self.subTest(const=const):
                surfaces = []
                for scalar, java_type in (("char_s", "byte"), ("char_u", "short")):
                    model = deepcopy(self.model_data)
                    alias = self.declaration(model, "MarshalPropertyName")
                    # Use a selected pointer typedef as a counted numeric span.
                    # Its declaration has no parameter-specific length policy.
                    char = {"kind": "scalar", "name": scalar, "bits": 8,
                            "const": const, "volatile": False}
                    alias["type"] = {"kind": "pointer", "target": char,
                                     "const": False, "volatile": False}
                    parameter = self.declaration(model, "SumValues")["parameters"][0]
                    parameter["type"] = {"kind": "alias", "name": alias["name"],
                                         "target": deepcopy(alias["type"]),
                                         "const": False, "volatile": False}
                    generator = self.make_generator(model, self.adapters)
                    java, _ = generator.generate()
                    self.assertIn(f"SumValues({java_type}[] pValues,", java)
                    surface = build_surface(generator, target="native")
                    self.assertEqual(surface["entries"][alias["name"]]["type"],
                                     ("const " if const else "") + scalar + ":8*")
                    self.assertEqual(surface["entries"]["Diligent::SumValues"]["parameters"][0]["type"],
                                     alias["name"])
                    surfaces.append(surface)
                self.assertNotEqual(surfaces[0], surfaces[1])

    def test_plain_char_remains_distinct_from_explicit_byte_types(self):
        baseline = self.surface()
        for name in ("schar", "uchar"):
            with self.subTest(name=name):
                model = deepcopy(self.model_data)
                element = self.declaration(model, "MarshalAnimationName")["type"]["element"]
                while element["kind"] == "alias":
                    element = element["target"]
                element["name"] = name
                actual = self.surface(model)["entries"]["Diligent::MarshalAnimationName"]
                self.assertNotEqual(actual, baseline["entries"]["Diligent::MarshalAnimationName"])

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
