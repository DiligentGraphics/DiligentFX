"""Generator contracts that are not observable through a successfully built JNI library."""

from copy import deepcopy
import importlib.util
import json
from pathlib import Path
import sys
import unittest

MODEL = json.loads(Path(sys.argv.pop(1)).read_text(encoding="utf-8"))
BACKEND = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("java_generator", BACKEND / "generate.py")
backend = importlib.util.module_from_spec(spec)
spec.loader.exec_module(backend)


ADAPTERS = backend.load_config(BACKEND.parent / "tests/native/Adapters.json")


def generate(model, **kwargs):
    adapters = dict(ADAPTERS, **kwargs.pop("adapters", {}))
    return backend.Generator(model, "Fixture", "example.api", "FixtureJNI", ["API.h"], adapters=adapters, **kwargs).generate()


def declaration(model, name):
    return next(d for d in model["declarations"] if d["name"] == "Diligent::" + name)


class GenerationTest(unittest.TestCase):
    def setUp(self):
        self.model = deepcopy(MODEL)

    def test_deterministic_and_does_not_mutate_shared_model(self):
        first = generate(self.model)
        self.assertEqual(first, generate(self.model))
        self.assertEqual(self.model, MODEL)
        self.assertIn("package example.api;", first[0])
        self.assertIn("Java_example_api_Fixture_", first[1])
        self.assertNotIn("diligent.bindings.tests", "\n".join(first))

    def test_native_defaults_are_not_reinterpreted_as_java_expressions(self):
        desc = declaration(self.model, "TestDesc")
        desc["fields"][0]["default"] = "ArbitraryNativeFactory()"
        java, cpp = generate(self.model)
        self.assertNotIn("ArbitraryNativeFactory", java)
        self.assertIn("ValueStorage<Diligent::TestDesc> Defaults;", cpp)

    def test_fixed_arguments_share_adapter_configuration(self):
        java, cpp = generate(self.model, adapters={
            "schema_version": 1,
            "fixed_arguments": {"Diligent::EchoTestMode": {"Value": "Diligent::TEST_MODE_LAST"}},
        })
        self.assertIn("EchoTestMode()", java)
        self.assertIn("Diligent::EchoTestMode(Diligent::TEST_MODE_LAST)", cpp)

    def test_scoped_bytes_share_adapters_and_hide_release_methods(self):
        java, cpp = generate(self.model)
        self.assertIn("public IMarshalBytes_MapReadResult MapRead()", java)
        self.assertIn("public ReadByteScope ppData;", java)
        self.assertIn("public WriteByteScope ppData;", java)
        self.assertNotIn("public int UnmapRead()", java)
        self.assertNotIn("public int UnmapWrite()", java)
        self.assertIn("Owner->MapRead(&Data)", cpp)
        self.assertIn("Scope->Acquired = Status == 3", cpp)
        self.assertIn("implements java.lang.AutoCloseable", java)

    def test_scoped_release_is_suppressed_on_its_declaring_interface(self):
        # Keep coverage for an inherited release that is not redeclared.
        interface = declaration(self.model, "IMarshalBytes")
        interface["methods"] = [m for m in interface["methods"] if m["name"] != "UnmapRead"]
        generator = backend.Generator(self.model, "Fixture", "example.api", "FixtureJNI", ["API.h"], ADAPTERS)
        self.assertEqual(generator.releases, {
            "Diligent::IMarshalReadRelease::UnmapRead",
            "Diligent::IMarshalBytes::UnmapWrite",
            "Diligent::IMarshalDerivedBytes::UnmapRead",
        })
        java, _ = generator.generate()
        self.assertIn("class IMarshalBytes extends IMarshalReadRelease", java)
        self.assertNotIn("public int UnmapRead()", java)

    def test_overridden_scope_release_hides_manual_and_automatic_bases(self):
        for binding in ("manual", "automatic"):
            with self.subTest(binding=binding):
                model = deepcopy(self.model)
                base = declaration(model, "IMarshalReadRelease")
                base["methods"][0]["policy"] = {"binding": binding}
                generator = backend.Generator(model, "Fixture", "example.api", "FixtureJNI", ["API.h"], ADAPTERS)
                java, cpp = generator.generate()
                self.assertEqual(generator.releases, {
                    "Diligent::IMarshalReadRelease::UnmapRead",
                    "Diligent::IMarshalBytes::UnmapRead",
                    "Diligent::IMarshalBytes::UnmapWrite",
                    "Diligent::IMarshalDerivedBytes::UnmapRead",
                })
                self.assertNotIn("public int UnmapRead()", java)
                self.assertNotIn("public int UnmapWrite()", java)
                # The native release still belongs to the scope adapter.
                self.assertIn("->UnmapRead()", cpp)

    def test_inherited_scope_hides_manual_and_automatic_descendant_releases(self):
        expected = generate(self.model)
        for binding in ("manual", "automatic"):
            for reverse in (False, True):
                with self.subTest(binding=binding, reverse=reverse):
                    model = deepcopy(self.model)
                    derived = declaration(model, "IMarshalDerivedBytes")
                    derived["methods"][0]["policy"] = {"binding": binding}
                    if reverse:
                        model["declarations"].reverse()
                    generator = backend.Generator(model, "Fixture", "example.api", "FixtureJNI", ["API.h"], ADAPTERS)
                    java, cpp = generator.generate()
                    self.assertIn("Diligent::IMarshalDerivedBytes::UnmapRead", generator.releases)
                    self.assertNotIn("public int UnmapRead()", java)
                    self.assertIn("class IMarshalDerivedBytes extends IMarshalBytes", java)
                    if not reverse:
                        # Both policies compile to the same tested Java/JNI wrapper.
                        self.assertEqual((java, cpp), expected)

    def test_unrelated_method_with_scope_release_name_stays_public(self):
        derived = declaration(self.model, "IMarshalDerivedBytes")
        derived["bases"] = ["Diligent::IObject"]
        java, _ = generate(self.model)
        self.assertIn("public int UnmapRead()", java)

    def test_scoped_bytes_require_explicit_adapters(self):
        with self.assertRaisesRegex(backend.ModelError, "manual function"):
            generate(self.model, adapters={"scoped_bytes": {}})

    def test_scoped_bytes_validate_acquire_release_and_size(self):
        for change in ({"size": "Missing"}, {"release": "Size"}, {"success": 2 ** 31}):
            with self.subTest(change=change):
                adapters = deepcopy(ADAPTERS)
                adapters["scoped_bytes"]["Diligent::IMarshalBytes::MapRead"].update(change)
                with self.assertRaises(backend.ModelError):
                    generate(self.model, adapters=adapters)

    def test_byte_scope_runtime_names_cannot_be_shadowed(self):
        for name in ("ReadByteScope", "WriteByteScope"):
            with self.subTest(name=name):
                model = deepcopy(self.model)
                model["declarations"].append({"kind": "record", "name": "Diligent::" + name, "fields": []})
                with self.assertRaisesRegex(backend.ModelError, "ambiguous declaration"):
                    generate(model)

    def test_rejects_unbounded_numeric_pointer(self):
        declaration(self.model, "SumValues")["parameters"][0]["policy"].pop("length")
        with self.assertRaisesRegex(backend.ModelError, "unsupported pointer"):
            generate(self.model)

    def test_rejects_inout_until_copyback_is_implemented(self):
        declaration(self.model, "CopyDesc")["parameters"][1]["policy"]["direction"] = "inout"
        with self.assertRaisesRegex(backend.ModelError, "inout"):
            generate(self.model)

    def test_computed_lengths_keep_counts_out_of_java_signatures(self):
        java, _ = generate(self.model)
        self.assertIn("float[] GetAllValues()", java)
        self.assertIn("MarshalOutputArrayItem[] GetAllItems()", java)
        self.assertIn("FillMatrices(IMarshalComputedOutputs pSource, boolean Fail, boolean Grow, boolean ResetCount)", java)
        self.assertIn("FillComputedValues(IMarshalComputedOutputs Source, MarshalDesc Options)", java)

    def test_rejects_invalid_length_methods(self):
        for name in ("Missing", "SetValueCount", "Update", "GetDesc"):
            with self.subTest(name=name):
                model = deepcopy(self.model)
                methods = declaration(model, "IMarshalArraySnapshots")["methods"]
                next(m for m in methods if m["name"] == "GetAllValues")["result"]["policy"]["length_method"] = name
                with self.assertRaises(backend.ModelError):
                    generate(model)
        # A free function has no receiver to provide length_method.
        result = declaration(self.model, "EchoValues")["result"]
        result["policy"].pop("length")
        result["policy"]["length_method"] = "GetValueCount"
        with self.assertRaisesRegex(backend.ModelError, "length_method"):
            generate(self.model)

    def test_computed_lengths_require_pointer_storage(self):
        for kind in ("reference", "scalar"):
            with self.subTest(kind=kind):
                model = deepcopy(self.model)
                methods = declaration(model, "IMarshalArraySnapshots")["methods"]
                result = next(m for m in methods if m["name"] == "GetAllValues")["result"]
                if kind == "reference":
                    result["type"]["kind"] = kind
                else:
                    result["type"] = result["type"]["target"]
                with self.assertRaisesRegex(backend.ModelError, "computed array lengths require a pointer"):
                    generate(model)

    def test_rejects_invalid_length_sources(self):
        for change in ("source", "nullable", "method", "nonconst", "field", "count_type", "descriptor_result"):
            with self.subTest(change=change):
                model = deepcopy(self.model)
                methods = {m["name"]: m for m in declaration(model, "IMarshalComputedOutputs")["methods"]}
                parameters = methods["FillMatrices"]["parameters"]
                query = parameters[1]["policy"]["length_from"]
                if change == "source":
                    query["parameter"] = "missing"
                elif change == "nullable":
                    parameters[0]["policy"]["nullable"] = True
                elif change == "method":
                    query["method"] = "SetCount"
                elif change == "nonconst":
                    methods["GetDesc"]["const"] = False
                elif change == "field":
                    query["field"] = "missing"
                elif change == "descriptor_result":
                    methods["GetDesc"]["result"]["type"]["kind"] = "pointer"
                else:
                    declaration(model, "MarshalComputedDesc")["fields"][0]["type"] = {
                        "kind": "scalar", "name": "int", "bits": 32, "const": False, "volatile": False}
                with self.assertRaisesRegex(backend.ModelError, "length_from"):
                    generate(model)

    def test_byte_spans_use_byte_arrays_without_changing_uint8_arrays(self):
        java, _ = generate(self.model)
        self.assertIn("ChecksumBytes(byte[] pBytes, long ByteCount, boolean Invert)", java)
        self.assertIn("CheckBytes(long ByteCount, short[] pBytes)", java)
        self.assertIn("public byte[] pBytes;", java)
        self.assertIn("byte[] GetData()", java)

    def test_rejects_unbounded_raw_byte_pointers(self):
        for name in ("ChecksumBytes", "FillByteCapacity"):
            with self.subTest(name=name):
                model = deepcopy(self.model)
                declaration(model, name)["parameters"][0]["policy"].pop("length")
                with self.assertRaisesRegex(backend.ModelError, "raw byte pointers require a length"):
                    generate(model)

    def test_raw_bytes_reject_retention_and_ownership_transfer(self):
        for target in ("parameter", "field", "result"):
            with self.subTest(target=target):
                model = deepcopy(self.model)
                if target == "parameter":
                    declaration(model, "ChecksumBytes")["parameters"][0]["policy"]["lifetime"] = "object"
                    error = "call lifetime"
                elif target == "field":
                    declaration(model, "MarshalBufferDesc")["fields"][1]["policy"]["lifetime"] = "object"
                    error = "call lifetime"
                else:
                    declaration(model, "EchoByteSpan")["result"]["policy"]["ownership"] = "owned"
                    error = "release adapter"
                with self.assertRaisesRegex(backend.ModelError, error):
                    generate(model)

    def test_raw_bytes_reject_mutable_inputs_and_volatile_storage(self):
        for target in ("parameter", "field", "volatile"):
            with self.subTest(target=target):
                model = deepcopy(self.model)
                value = (declaration(model, "MarshalBufferDesc")["fields"][1] if target == "field"
                         else declaration(model, "ChecksumBytes")["parameters"][0])
                if target == "volatile":
                    value["type"]["target"]["volatile"] = True
                    error = "non-volatile"
                else:
                    value["type"]["target"]["const"] = False
                    error = "copyback adapter"
                with self.assertRaisesRegex(backend.ModelError, error):
                    generate(model)

    def test_rejects_multiple_interface_bases(self):
        declaration(self.model, "ITestCounter")["bases"].append("Diligent::IMarshalObject")
        with self.assertRaisesRegex(backend.ModelError, "interfaces need one base"):
            generate(self.model)

    def test_rejects_cyclic_interface_inheritance(self):
        declaration(self.model, "IMarshalObject")["bases"] = ["Diligent::IMarshalMutableObject"]
        with self.assertRaisesRegex(backend.ModelError, "cyclic interface inheritance"):
            generate(self.model)

    def test_rejects_non_reference_counted_interface_base(self):
        declaration(self.model, "IObject")["policy"].pop("ownership")
        with self.assertRaisesRegex(backend.ModelError, "reference-counted external type"):
            generate(self.model)

    def test_rejects_interface_data_fields(self):
        declaration(self.model, "ITestCounter")["fields"] = declaration(self.model, "TestVector")["fields"]
        with self.assertRaisesRegex(backend.ModelError, "no data fields"):
            generate(self.model)

    def test_rejects_interface_runtime_method_collisions(self):
        for name in ("close", "QueryInterface"):
            with self.subTest(name=name):
                model = deepcopy(MODEL)
                declaration(model, "ITestCounter")["methods"][0]["name"] = name
                with self.assertRaisesRegex(backend.ModelError, "conflicts with the runtime"):
                    generate(model)

    def test_overrides_do_not_depend_on_declaration_order(self):
        self.model["declarations"].reverse()
        java, _ = generate(self.model)
        self.assertIn("class MarshalObjectDesc extends MarshalObjectAttribs", java)
        self.assertEqual(java.count("class IMarshalBaseOverrides_GetValueResult"), 1)
        self.assertNotIn("IMarshalDerivedOverrides_GetValueResult", java)

    def test_renamed_override_outputs_keep_base_result_fields(self):
        method = next(m for m in declaration(self.model, "IMarshalDerivedOverrides")["methods"]
                      if m["name"] == "GetValues")
        for parameter in method["parameters"]:
            parameter["name"] = "Renamed" + parameter["name"]
        java, cpp = generate(self.model)
        self.assertNotIn("IMarshalDerivedOverrides_GetValuesResult", java)
        self.assertNotIn('"RenamedValue"', cpp)
        self.assertNotIn('"RenamedSize"', cpp)
        self.assertIn('Field(Env, Result, "Value", "J")', cpp)
        self.assertIn('Field(Env, Result, "Size", "J")', cpp)

    def test_rejects_incompatible_override_output_results(self):
        method = next(m for m in declaration(self.model, "IMarshalDerivedOverrides")["methods"]
                      if m["name"] == "GetValue")
        method["parameters"][0]["type"]["target"] = deepcopy(declaration(self.model, "EchoFloat")["result"]["type"])
        with self.assertRaisesRegex(backend.ModelError, "incompatible output result in override"):
            generate(self.model)

    def test_rejects_multiple_descriptor_bases(self):
        declaration(self.model, "MarshalObjectDesc")["bases"].append("Diligent::TestVector")
        with self.assertRaisesRegex(backend.ModelError, "descriptors support only single inheritance"):
            generate(self.model)

    def test_rejects_name_collisions(self):
        extra = deepcopy(declaration(self.model, "EchoInt8"))
        extra["name"] = "AnotherNamespace::EchoInt8"
        self.model["declarations"].append(extra)
        with self.assertRaisesRegex(backend.ModelError, "ambiguous declaration"):
            generate(self.model)

    def test_rejects_manual_function_without_adapter(self):
        declaration(self.model, "EchoInt8")["policy"] = {"binding": "manual"}
        with self.assertRaisesRegex(backend.ModelError, "manual function"):
            generate(self.model)

    def test_rejects_retained_input_storage(self):
        for function, parameter in (("EchoText", "Text"), ("SumValues", "pValues"),
                                    ("CopyDesc", "Desc"), ("EchoOptionalDesc", "pDesc")):
            for lifetime in ("object", "static", "caller"):
                with self.subTest(function=function, lifetime=lifetime):
                    model = deepcopy(MODEL)
                    value = next(p for p in declaration(model, function)["parameters"] if p["name"] == parameter)
                    value["policy"]["lifetime"] = lifetime
                    with self.assertRaisesRegex(backend.ModelError, "call lifetime"):
                        generate(model)

    def test_rejects_input_ownership_transfer(self):
        declaration(self.model, "EchoText")["parameters"][0]["policy"]["ownership"] = "owned"
        with self.assertRaisesRegex(backend.ModelError, "transfer adapter"):
            generate(self.model)

    def test_rejects_retained_nested_input_fields(self):
        for record in ("TestDesc", "RequiredDesc"):
            with self.subTest(record=record):
                model = deepcopy(MODEL)
                field = next(f for f in declaration(model, record)["fields"] if f["name"] == "Name")
                field["policy"]["lifetime"] = "object"
                with self.assertRaisesRegex(backend.ModelError, record + "::Name.*call lifetime"):
                    generate(model)

    def test_rejects_retained_interface_arrays(self):
        declaration(self.model, "SumInterfaceArray")["parameters"][0]["policy"]["lifetime"] = "object"
        with self.assertRaisesRegex(backend.ModelError, "call lifetime"):
            generate(self.model)

    def test_rejects_retained_interface_fields(self):
        for field_name in ("pObject", "ppObjects"):
            with self.subTest(field=field_name):
                model = deepcopy(MODEL)
                field = next(f for f in declaration(model, "MarshalInterfaceDesc")["fields"] if f["name"] == field_name)
                field["policy"]["lifetime"] = "object"
                with self.assertRaisesRegex(backend.ModelError, field_name + ".*call lifetime"):
                    generate(model)

    def test_rejects_owned_interface_fields(self):
        # Output-only snapshots cannot consume refs from the native descriptor.
        self.model["declarations"] = [d for d in self.model["declarations"] if d["kind"] != "function"]
        field = next(f for f in declaration(self.model, "MarshalInterfaceDesc")["fields"] if f["name"] == "pObject")
        field["policy"]["ownership"] = "owned"
        with self.assertRaisesRegex(backend.ModelError, "interface fields must borrow"):
            generate(self.model)

    def test_rejects_owned_interface_array_results(self):
        declaration(self.model, "EchoInterfaceArray")["result"]["policy"]["ownership"] = "owned"
        with self.assertRaisesRegex(backend.ModelError, "release adapter"):
            generate(self.model)

    def test_rejects_retained_data_in_dynamic_descriptor_arrays(self):
        for field_name in ("Name", "pValues"):
            with self.subTest(field=field_name):
                model = deepcopy(MODEL)
                field = next(f for f in declaration(model, "MarshalArrayItem")["fields"] if f["name"] == field_name)
                field["policy"]["lifetime"] = "object"
                with self.assertRaisesRegex(backend.ModelError, field_name + ".*call lifetime"):
                    generate(model)

    def test_rejects_owned_dynamic_descriptor_array_results(self):
        method = next(m for m in declaration(self.model, "IMarshalArraySnapshots")["methods"] if m["name"] == "GetItems")
        method["result"]["policy"]["ownership"] = "owned"
        with self.assertRaisesRegex(backend.ModelError, "release adapter"):
            generate(self.model)

    def test_dynamic_descriptor_dependencies_ignore_declaration_order(self):
        self.model["declarations"].reverse()
        _, cpp = generate(self.model)
        self.assertLess(cpp.index("struct InputMarshalArrayItem\n"), cpp.index("struct InputMarshalArrayDesc\n"))

    def test_rejects_recursive_descriptor_arrays(self):
        field = next(f for f in declaration(self.model, "MarshalArrayDesc")["fields"] if f["name"] == "pItems")
        field["type"]["target"] = {"kind": "named", "name": "Diligent::MarshalArrayDesc", "const": True, "volatile": False}
        with self.assertRaisesRegex(backend.ModelError, "recursive descriptor storage"):
            generate(self.model)

    def test_rejects_retained_descriptor_pointer_fields(self):
        field = declaration(self.model, "MarshalPointerDesc")["fields"][0]
        field["policy"]["lifetime"] = "object"
        with self.assertRaisesRegex(backend.ModelError, "MarshalPointerDesc::pData.*call lifetime"):
            generate(self.model)

    def test_rejects_owned_descriptor_pointer_fields(self):
        field = declaration(self.model, "MarshalPointerDesc")["fields"][0]
        field["policy"]["ownership"] = "owned"
        with self.assertRaisesRegex(backend.ModelError, "release adapter"):
            generate(self.model)

    def test_rejects_mutable_descriptor_pointer_fields(self):
        field = declaration(self.model, "MarshalPointerDesc")["fields"][0]
        field["type"]["target"]["const"] = False
        with self.assertRaisesRegex(backend.ModelError, "copyback adapter"):
            generate(self.model)

    def test_rejects_recursive_descriptor_pointer_fields(self):
        field = declaration(self.model, "MarshalPointerDesc")["fields"][0]
        field["type"]["target"] = {"kind": "named", "name": "Diligent::MarshalPointerDesc", "const": True, "volatile": False}
        with self.assertRaisesRegex(backend.ModelError, "recursive descriptor storage"):
            generate(self.model)

    def test_pointer_pointees_precede_enclosing_descriptors(self):
        self.model["declarations"].reverse()
        _, cpp = generate(self.model)
        self.assertLess(cpp.index("struct InputMarshalDesc\n"), cpp.index("struct InputMarshalPointerDesc\n"))
        self.assertLess(cpp.index("struct InputMarshalPointerDesc\n"), cpp.index("struct InputMarshalNestedPointerDesc\n"))

    def test_output_only_borrowed_storage_can_have_object_lifetime(self):
        # The returned descriptor is copied while its native owner is still alive.
        self.model["declarations"] = [d for d in self.model["declarations"]
                                      if d["kind"] != "function" or d["name"] == "Diligent::GetIncompleteDesc"]
        field = declaration(self.model, "RequiredDesc")["fields"][0]
        field["policy"]["lifetime"] = "object"
        generate(self.model)

    def test_rejects_owned_descriptor_pointer_results(self):
        result = declaration(self.model, "EchoOptionalDesc")["result"]
        result["policy"].update(ownership="owned", lifetime="caller")
        with self.assertRaisesRegex(backend.ModelError, "release adapter"):
            generate(self.model)

    def test_rejects_owned_descriptor_pointer_outputs(self):
        parameter = declaration(self.model, "CopyDescPointer")["parameters"][1]
        parameter["type"] = dict(parameter["type"], target=deepcopy(parameter["type"]))
        parameter["policy"].update(ownership="owned", lifetime="caller")
        with self.assertRaisesRegex(backend.ModelError, "release adapter"):
            generate(self.model)

    def test_runtime_types_are_qualified(self):
        java, _ = generate(self.model)
        for name in ("java.lang.String", "java.lang.System", "java.lang.Runnable",
                     "java.lang.AutoCloseable", "java.lang.Override", "java.lang.ref.Cleaner", "java.lang.Class"):
            self.assertTrue(name in java, name)

    def test_rejects_runtime_package_shadowing(self):
        extra = deepcopy(declaration(self.model, "TestVector"))
        extra["name"] = "Diligent::java"
        self.model["declarations"].append(extra)
        with self.assertRaisesRegex(backend.ModelError, "ambiguous declaration"):
            generate(self.model)
        with self.assertRaisesRegex(backend.ModelError, "reserved module"):
            backend.Generator(MODEL, "java", "example.api", "FixtureJNI", ["API.h"])

    def test_rejects_invalid_fixed_array_extents(self):
        for count in (0, -1, 2 ** 31):
            with self.subTest(count=count):
                model = deepcopy(MODEL)
                declaration(model, "TestFixedArrays")["fields"][0]["type"]["count"] = count
                with self.assertRaisesRegex(backend.ModelError, "extent"):
                    generate(model)

    def test_rejects_fixed_array_length_policy(self):
        declaration(self.model, "TestFixedArrays")["fields"][0].setdefault("policy", {})["length"] = 8
        with self.assertRaisesRegex(backend.ModelError, "declared extent"):
            generate(self.model)

    def test_rejects_unsupported_fixed_array_elements(self):
        pointer = deepcopy(declaration(MODEL, "EchoText")["parameters"][0]["type"])
        scalar = deepcopy(declaration(MODEL, "TestFixedArrays")["fields"][0]["type"]["element"])
        for element, error in ((pointer, "fixed arrays support"),
                               (dict(scalar, const=True), "writable elements"),
                               (dict(scalar, volatile=True), "volatile")):
            with self.subTest(element=element):
                model = deepcopy(MODEL)
                declaration(model, "TestFixedArrays")["fields"][0]["type"]["element"] = element
                with self.assertRaisesRegex(backend.ModelError, error):
                    generate(model)

    def test_rejects_retained_strings_in_fixed_descriptor_arrays(self):
        field = next(f for f in declaration(self.model, "TestFixedChild")["fields"] if f["name"] == "Label")
        field["policy"]["lifetime"] = "object"
        with self.assertRaisesRegex(backend.ModelError, "TestFixedChild::Label.*call lifetime"):
            generate(self.model)

    def test_rejects_non_scalar_constants(self):
        types = [declaration(MODEL, "EchoText")["result"]["type"],
                 declaration(MODEL, "TestFixedArrays")["fields"][0]["type"],
                 declaration(MODEL, "TestDesc")["fields"][4]["type"]]
        for native_type in types:
            with self.subTest(native_type=native_type):
                model = deepcopy(MODEL)
                declaration(model, "TestConstantInvalidID")["type"] = deepcopy(native_type)
                with self.assertRaisesRegex(backend.ModelError, "only scalar constants"):
                    generate(model)

    def test_rejects_manual_constant_without_adapter(self):
        declaration(self.model, "TestConstantInvalidID")["policy"] = {
            "binding": "manual", "reason": "The constant needs a language adapter."
        }
        with self.assertRaisesRegex(backend.ModelError, "manual constant requires an adapter.*TestConstantInvalidID"):
            generate(self.model)

    def test_rejects_constant_runtime_name_collision(self):
        declaration(self.model, "TestConstantInvalidID")["name"] = "Diligent::CLEANER"
        with self.assertRaisesRegex(backend.ModelError, "ambiguous declaration"):
            generate(self.model)

    def test_rejects_java_keywords(self):
        declaration(self.model, "EchoInt8")["parameters"][0]["name"] = "switch"
        with self.assertRaisesRegex(backend.ModelError, "unsupported identifier"):
            generate(self.model)


if __name__ == "__main__":
    unittest.main()
