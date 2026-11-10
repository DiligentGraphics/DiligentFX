"""API baseline tests over real fixture declarations, without invoking Clang."""

from copy import deepcopy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from binding_model import ModelError
from javascript.api_surface import build_surface, format_surface
from javascript.generate import Generator

MODEL = json.loads(Path(sys.argv.pop(1)).read_text(encoding="utf-8"))
ADAPTERS = json.loads(Path(__file__).with_name("Adapters.json").read_text(encoding="utf-8"))


class APISurfaceTest(unittest.TestCase):
    def surface(self, model=None, adapters=None):
        generator = Generator(model or MODEL, "BindingTests", ["Interfaces.h"],
                              ADAPTERS if adapters is None else adapters)
        generator.generate()
        return build_surface(generator)

    def declaration(self, model, name):
        return next(d for d in model["declarations"] if d["name"] == "Diligent::" + name)

    def test_records_signatures_defaults_constants_aliases_and_inheritance(self):
        entries = self.surface()["entries"]
        self.assertEqual(entries["Diligent::MarshalDesc::Count"],
                         {"kind": "field", "type": "int:32", "default": "- 13"})
        self.assertEqual(entries["Diligent::MarshalInvalidID"]["value"], 2 ** 64 - 1)
        self.assertEqual(entries["Diligent::MarshalEntityAlias"]["type"], "Diligent::MarshalEntityID")
        self.assertEqual(entries["Diligent::MarshalExportChild"]["bases"], ["Diligent::MarshalExportBase"])
        method = entries["Diligent::IMarshalObject::GetSize"]
        self.assertTrue(method["const"])
        self.assertEqual(method["result"]["type"], "ulonglong:64")
        self.assertEqual(method["parameters"], [])
        self.assertEqual(entries["Diligent::WithDefaults"]["parameters"][1]["default"], "True")
        self.assertEqual(entries["Diligent::BorrowObject"]["result"]["policy"]["ownership"], "borrowed")
        self.assertTrue(any(e["kind"] == "enumerator" for e in entries.values()))
        self.assertEqual(entries["Diligent::MarshalExportNames::Parts"]["type"], "uint:32[2]")
        self.assertEqual(entries["Diligent::MarshalAnimationName"]["type"], "const char_s:8[12]")

    def test_ignores_documentation_locations_and_declaration_order(self):
        model = deepcopy(MODEL)
        def change_metadata(item):
            if isinstance(item, dict):
                if "source" in item:
                    item["source"] = {"file": "another/checkout/API.h", "line": 999}
                if "documentation" in item:
                    item["documentation"] = "Changed documentation"
                if "reason" in item:
                    item["reason"] = "Changed explanation"
                for value in item.values():
                    change_metadata(value)
            elif isinstance(item, list):
                for value in item:
                    change_metadata(value)
        change_metadata(model)
        model["declarations"].reverse()
        for declaration in model["declarations"]:
            for key in ("methods", "fields", "values"):
                declaration.get(key, []).reverse()
        original = deepcopy(MODEL)
        self.assertEqual(format_surface(self.surface(model)), format_surface(self.surface()))
        self.assertEqual(MODEL, original)

    def test_detects_method_addition_removal_rename_and_signature_changes(self):
        baseline = self.surface()
        for change in ("add", "remove", "rename", "result", "const"):
            with self.subTest(change=change):
                model = deepcopy(MODEL)
                methods = self.declaration(model, "IMarshalObject")["methods"]
                method = next(m for m in methods if m["name"] == "GetRefCount")
                if change == "add":
                    methods.append(dict(deepcopy(method), name="NewMethod"))
                elif change == "remove":
                    methods.remove(method)
                elif change == "rename":
                    method["name"] = "RenamedMethod"
                elif change == "result":
                    method["result"]["type"] = deepcopy(self.declaration(model, "GetInvalidEntityID")["result"]["type"])
                else:
                    method["const"] = not method["const"]
                self.assertNotEqual(self.surface(model), baseline)

    def test_detects_field_default_array_extent_and_parameter_order_changes(self):
        baseline = self.surface()
        for change in ("field", "default", "array", "order", "argument_default"):
            with self.subTest(change=change):
                model = deepcopy(MODEL)
                if change == "field":
                    self.declaration(model, "MarshalDesc")["fields"].pop()
                elif change == "default":
                    self.declaration(model, "MarshalDesc")["fields"][0]["default"] = "0"
                elif change == "array":
                    field = self.declaration(model, "MarshalExportNames")["fields"][2]
                    field["type"]["count"] = 3
                else:
                    parameters = self.declaration(model, "WithDefaults")["parameters"]
                    if change == "order":
                        parameters[1:] = reversed(parameters[1:])
                    else:
                        parameters[1]["default"] = "False"
                self.assertNotEqual(self.surface(model), baseline)

    def test_detects_enum_constant_and_typedef_changes(self):
        baseline = self.surface()
        for change in ("enum", "constant", "alias"):
            with self.subTest(change=change):
                model = deepcopy(MODEL)
                if change == "enum":
                    self.declaration(model, "MARSHAL_MODE")["values"][0]["value"] += 1
                elif change == "constant":
                    self.declaration(model, "MarshalInvalidID")["value"] -= 1
                else:
                    self.declaration(model, "MarshalEntityAlias")["type"] = deepcopy(
                        self.declaration(model, "MarshalEntityID")["type"])
                self.assertNotEqual(self.surface(model), baseline)

    def test_uses_effective_policies_and_adapter_contracts(self):
        baseline = self.surface()
        entries = baseline["entries"]
        self.assertNotIn("Diligent::IMarshalBytes::UnmapRead", entries)
        self.assertEqual(entries["Diligent::IMarshalBytes::MapRead"]["scope"],
                         {"release": "UnmapRead", "size": "Size", "success": 0, "writable": False})
        self.assertEqual(entries["Diligent::MarshalNestedCountDesc::pComputed"]["count_function"],
                         "Diligent::GetComputedCount")
        adapters = deepcopy(ADAPTERS)
        adapters["scoped_bytes"]["Diligent::IMarshalBytes::MapRead"]["success"] = 1
        self.assertNotEqual(self.surface(adapters=adapters), baseline)
        adapters = deepcopy(ADAPTERS)
        fixed = {"Status": "Diligent::MARSHAL_STATUS_OK"}
        adapters["fixed_arguments"] = {"Diligent::CreateMarshalObject": fixed}
        self.assertEqual(self.surface(adapters=adapters)["entries"]["Diligent::CreateMarshalObject"]["fixed_arguments"], fixed)
        model = deepcopy(MODEL)
        self.declaration(model, "BorrowObject")["result"]["policy"].update(ownership="owned", lifetime="caller")
        self.assertNotEqual(self.surface(model), baseline)

    def test_native_only_fields_are_omitted(self):
        adapters = deepcopy(ADAPTERS)
        adapters["native_only"] = ["Diligent::MarshalDesc::Weight"]
        baseline = self.surface(adapters=adapters)
        self.assertNotIn("Diligent::MarshalDesc::Weight", baseline["entries"])
        model = deepcopy(MODEL)
        field = next(f for f in self.declaration(model, "MarshalDesc")["fields"] if f["name"] == "Weight")
        field["default"] = "4.5"
        self.assertEqual(self.surface(model, adapters), baseline)
        self.assertNotEqual(baseline, self.surface())

    def test_requires_wasm32(self):
        model = dict(MODEL, target="x86_64-unknown-linux-gnu")
        with self.assertRaisesRegex(ModelError, "Wasm32"):
            self.surface(model)

    def test_check_diffs_without_modifying_baseline_and_update_is_explicit(self):
        surface = self.surface()
        with tempfile.TemporaryDirectory() as directory:
            actual = Path(directory) / "actual.json"
            baseline = Path(directory) / "baseline.json"
            actual.write_text(format_surface(surface), encoding="utf-8")
            command = [sys.executable, "-B", str(Path(__file__).parents[1] / "api_surface.py"),
                       "--actual", str(actual), "--baseline", str(baseline)]
            def run(*args):
                return subprocess.run([*command, *args], capture_output=True, text=True)
            result = run()
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("baseline is missing", result.stderr)
            self.assertFalse(baseline.exists())
            self.assertEqual(run("--update").returncode, 0)
            self.assertEqual(run().returncode, 0)
            original = baseline.read_bytes()
            surface["entries"]["Diligent::MarshalInvalidID"]["value"] -= 1
            actual.write_text(format_surface(surface), encoding="utf-8")
            result = run()
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Binding API surface changed", result.stderr)
            self.assertIn("@@", result.stderr)
            self.assertIn("Diligent::MarshalInvalidID", result.stderr)
            self.assertEqual(baseline.read_bytes(), original)
            self.assertEqual(run("--update").returncode, 0)
            self.assertEqual(run().returncode, 0)
            self.assertNotEqual(baseline.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
