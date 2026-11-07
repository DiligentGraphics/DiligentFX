"""Emitter tests over a serialized model, with no libclang imports."""

from copy import deepcopy
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from javascript.generate import Generator, write
from binding_model import ModelError

MODEL = json.loads(Path(sys.argv.pop(1)).read_text(encoding="utf-8"))


class GeneratorTest(unittest.TestCase):
    def emit(self, model=None):
        return Generator(model or MODEL, "BindingTests", ["Interfaces.h"]).generate()

    def declaration(self, model, name):
        return next(d for d in model["declarations"] if d["name"] == "Diligent::" + name)

    def test_deterministic_generation_and_unchanged_output(self):
        output = self.emit()
        self.assertEqual(output, self.emit(json.loads(json.dumps(MODEL))))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "generated.cpp"
            write(path, output[0])
            before = path.stat().st_mtime_ns
            write(path, output[0])
            self.assertEqual(before, path.stat().st_mtime_ns)

    def test_native_defaults_and_opaque_dispatch(self):
        cpp, js, declarations = self.emit()
        self.assertIn("new (std::nothrow) Diligent::MarshalDesc{}", cpp)
        self.assertIn("reinterpret_cast<Diligent::IMarshalObject*>(self)->GetSize()", cpp)
        self.assertNotIn("vtable", js)
        self.assertNotIn("clang", sys.modules)
        self.assertIn("Size?: bigint", declarations)
        self.assertIn("Enabled?: boolean", declarations)
        self.assertIn("IMarshalMutableObject extends IMarshalObject", declarations)
        self.assertIn("Result<IMarshalObject | null>", declarations)

    def test_rejects_wrong_model_target(self):
        model = deepcopy(MODEL)
        model["target"] = "x86_64-pc-windows-msvc"
        with self.assertRaisesRegex(ModelError, "Wasm32"):
            self.emit(model)

    def test_scalar_only_api_has_no_iobject_dependency(self):
        model = deepcopy(MODEL)
        model["declarations"] = [self.declaration(model, "EchoInt32")]
        cpp, _, _ = self.emit(model)
        self.assertNotIn("IObject", cpp)
        self.assertNotIn("binding_Release", cpp)

    def test_rejects_manual_members_without_an_adapter(self):
        model = deepcopy(MODEL)
        self.declaration(model, "MarshalDesc")["fields"][0]["policy"] = {"binding": "manual", "reason": "test"}
        with self.assertRaisesRegex(ModelError, "explicit JavaScript adapter"):
            self.emit(model)

    def test_rejects_unimplemented_pointer_and_array_shapes(self):
        for kind in ["pointer", "array"]:
            model = deepcopy(MODEL)
            field = self.declaration(model, "MarshalDesc")["fields"][0]
            if kind == "pointer":
                field["type"] = {"kind": kind, "target": field["type"], "const": False, "volatile": False}
            else:
                field["type"] = {"kind": kind, "element": field["type"], "count": 2, "const": False, "volatile": False}
            with self.assertRaisesRegex(ModelError, "expected a scalar"):
                self.emit(model)

    def test_rejects_unsupported_ownership(self):
        model = deepcopy(MODEL)
        self.declaration(model, "CreateMarshalObject")["parameters"][-1]["policy"]["ownership"] = "borrowed"
        with self.assertRaisesRegex(ModelError, "unsupported output ownership"):
            self.emit(model)

    def test_rejects_default_arguments_instead_of_evaluating_cpp(self):
        model = deepcopy(MODEL)
        self.declaration(model, "EchoInt32")["parameters"][0]["default"] = "42"
        with self.assertRaisesRegex(ModelError, "default arguments"):
            self.emit(model)

    def test_rejects_runtime_name_collisions(self):
        model = deepcopy(MODEL)
        self.declaration(model, "EchoInt32")["name"] = "Diligent::Result"
        with self.assertRaisesRegex(ModelError, "conflicting JavaScript name"):
            self.emit(model)


if __name__ == "__main__":
    unittest.main()
