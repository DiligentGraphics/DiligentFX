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
ADAPTERS = json.loads(Path(__file__).with_name("Adapters.json").read_text(encoding="utf-8"))


class GeneratorTest(unittest.TestCase):
    def emit(self, model=None, adapters=None):
        return Generator(model or MODEL, "BindingTests", ["Interfaces.h"],
                         ADAPTERS if adapters is None else adapters).generate()

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

    def test_nested_fields_use_native_subobjects_and_types(self):
        generator = Generator(MODEL, "BindingTests", ["Interfaces.h"], ADAPTERS)
        cpp, _, types = generator.generate()
        fields = generator.schema["records"]["MarshalNestedDesc"]["fields"]
        self.assertEqual(fields["Transform"], {
            "record": "MarshalNestedTransform",
            "address": "_binding_Diligent_MarshalNestedDesc_Field_Transform"})
        self.assertIn("&reinterpret_cast<Diligent::MarshalNestedDesc*>(pointer)->Transform", cpp)
        self.assertIn("Transform?: MarshalNestedTransform;", types)
        # A typedef to a child struct uses the same canonical descriptor.
        scale = generator.schema["records"]["MarshalNestedTransform"]["fields"]["Scale"]
        self.assertEqual(scale["record"], "MarshalNestedVector")
        self.assertIn("Scale?: MarshalNestedVector;", types)

    def test_nested_fields_reject_const_and_volatile(self):
        for qualifier, message in [("const", "const fields"), ("volatile", "Volatile values")]:
            with self.subTest(qualifier=qualifier):
                model = deepcopy(MODEL)
                field = self.declaration(model, "MarshalNestedDesc")["fields"][1]
                field["type"][qualifier] = True
                with self.assertRaisesRegex(ModelError, message):
                    self.emit(model)

    def test_rejects_wrong_model_target(self):
        model = deepcopy(MODEL)
        model["target"] = "x86_64-pc-windows-msvc"
        with self.assertRaisesRegex(ModelError, "Wasm32"):
            self.emit(model)

    def test_scalar_only_api_has_no_iobject_dependency(self):
        model = deepcopy(MODEL)
        model["declarations"] = [self.declaration(model, "EchoInt32")]
        cpp, _, _ = self.emit(model, {})
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

    def test_byte_spans_and_scopes_have_explicit_types(self):
        cpp, js, declarations = self.emit()
        self.assertIn("pBytes?: Uint8Array | null", declarations)
        self.assertIn("ChecksumBytes(pBytes: Uint8Array, ByteCount: bigint, Invert: boolean)", declarations)
        self.assertIn("MapRead(): Result<ReadByteAccess | null>", declarations)
        self.assertIn("MapWrite(): Result<WriteByteAccess | null>", declarations)
        self.assertNotIn("UnmapRead()", declarations)
        self.assertNotIn("UnmapWrite()", declarations)
        self.assertIn("->UnmapRead()", cpp)
        self.assertIn("MapRead(reinterpret_cast<const void**>(output))", cpp)
        self.assertIn('"success": 3', js)

    def test_strings_have_explicit_types_and_support_both_output_forms(self):
        cpp, _, types = self.emit()
        self.assertIn("Name?: string;", types)
        self.assertIn("Label?: string | null;", types)
        self.assertIn("EchoString(Text: string, GrowHeap: boolean): string;", types)
        self.assertIn("EchoNullableString(Text: string | null): string | null;", types)
        self.assertIn("GetText(): string | null;", types)
        self.assertIn("ReadText(Fail: boolean): Result<string | null>;", types)
        self.assertIn("ReadTextPointer(Fail: boolean): Result<string | null>;", types)
        self.assertIn("CheckCharacterBytes(ByteCount: number, Bytes: Uint8Array): boolean;", types)
        self.assertIn("*reinterpret_cast<const Diligent::Char**>(Text)", cpp)
        self.assertIn("reinterpret_cast<const Diligent::Char**>(ppText)", cpp)

    def test_strings_reject_mutable_owned_and_retained_inputs(self):
        for change in ["mutable", "ownership", "lifetime", "direction"]:
            with self.subTest(change=change):
                model = deepcopy(MODEL)
                field = self.declaration(model, "MarshalStringDesc")["fields"][1]
                if change == "mutable":
                    field["type"]["target"]["const"] = False
                else:
                    field["policy"][change] = {"ownership": "owned", "lifetime": "object", "direction": "out"}[change]
                with self.assertRaisesRegex(ModelError, "strings require borrowed const char"):
                    self.emit(model)

    def test_strings_reject_unsupported_output_ownership_and_lifetimes(self):
        for name, key, value in [("GetStaticString", "ownership", "owned"),
                                 ("GetStaticString", "lifetime", "read_scope"),
                                 ("EchoString", "direction", "in")]:
            with self.subTest(name=name, key=key):
                model = deepcopy(MODEL)
                self.declaration(model, name)["result"]["policy"][key] = value
                with self.assertRaisesRegex(ModelError, "strings require borrowed const char"):
                    self.emit(model)

    def test_scope_size_query_can_be_inherited(self):
        model = deepcopy(MODEL)
        interface = self.declaration(model, "IMarshalBytes")
        interface["bases"] = ["Diligent::IMarshalObject"]
        adapters = deepcopy(ADAPTERS)
        for scope in adapters["scoped_bytes"].values():
            scope["size"] = "GetSize"
        cpp, _, _ = self.emit(model, adapters)
        self.assertIn("reinterpret_cast<Diligent::IMarshalBytes*>(self)->GetSize()", cpp)

    def test_scope_methods_require_an_explicit_adapter(self):
        with self.assertRaisesRegex(ModelError, "explicit JavaScript adapter"):
            self.emit(adapters={})

    def test_scope_adapter_rejects_wrong_methods_types_and_modes(self):
        for key, value, error in [
            ("size", "NoSuchMethod", "must belong"),
            ("release", "NoSuchMethod", "must belong"),
            ("writable", True, "matching constness"),
            ("success", 2**32, "32-bit signed status"),
            ("writable", "false", "invalid scoped byte"),
        ]:
            with self.subTest(key=key, value=value):
                adapters = deepcopy(ADAPTERS)
                adapters["scoped_bytes"]["Diligent::IMarshalBytes::MapRead"][key] = value
                with self.assertRaisesRegex(ModelError, error):
                    self.emit(adapters=adapters)

    def test_byte_spans_reject_retention_and_non_byte_data(self):
        for change in ["lifetime", "type", "length"]:
            with self.subTest(change=change):
                model = deepcopy(MODEL)
                field = self.declaration(model, "MarshalBufferDesc")["fields"][1]
                if change == "lifetime":
                    field["policy"]["lifetime"] = "object"
                elif change == "type":
                    field["type"]["target"]["const"] = False
                else:
                    field["policy"]["length"] = "NoSuchField"
                with self.assertRaisesRegex(ModelError, "byte spans require"):
                    self.emit(model)

    def test_native_only_fields_and_fixed_arguments_preserve_the_model(self):
        adapters = deepcopy(ADAPTERS)
        adapters["native_only"] = ["Diligent::MarshalDesc::Weight"]
        adapters["fixed_arguments"] = {"Diligent::CreateMarshalObject": {"Status": "Diligent::MARSHAL_STATUS_OK"}}
        original = deepcopy(MODEL)
        cpp, _, types = self.emit(adapters=adapters)
        self.assertEqual(MODEL, original)
        self.assertNotIn("Weight?:", types)
        self.assertIn("CreateMarshalObject(Desc: MarshalDesc, Mutable: boolean)", types)
        self.assertIn("Diligent::MARSHAL_STATUS_OK, Mutable,", cpp)

    def test_field_policy_override_is_local_to_the_backend(self):
        model = deepcopy(MODEL)
        field = self.declaration(model, "MarshalBufferDesc")["fields"][1]
        adapters = deepcopy(ADAPTERS)
        adapters["field_policies"] = {"Diligent::MarshalBufferDesc": {"pBytes": deepcopy(field["policy"])}}
        field["policy"] = {"binding": "manual", "reason": "Backend chooses lifetime"}
        _, _, types = self.emit(model, adapters)
        self.assertIn("pBytes?: Uint8Array | null", types)
        self.assertEqual(field["policy"]["binding"], "manual")

    def test_invalid_subset_configuration_is_rejected(self):
        for section, config, error in [
            ("native_only", ["Diligent::MarshalDesc::NoSuchField"], "native-only selection"),
            ("fixed_arguments", {"Diligent::CreateMarshalObject": {"Status": "Diligent::MARSHAL_MODE_FIRST"}}, "enumerator"),
            ("field_policies", {"Diligent::MarshalDesc": {"NoSuchField": {}}}, "unknown metadata keys"),
        ]:
            with self.subTest(section=section):
                adapters = deepcopy(ADAPTERS)
                adapters[section] = config
                with self.assertRaisesRegex(ModelError, error):
                    self.emit(adapters=adapters)


if __name__ == "__main__":
    unittest.main()
