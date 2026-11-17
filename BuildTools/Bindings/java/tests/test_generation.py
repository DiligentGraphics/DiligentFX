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


def generate(model, **kwargs):
    return backend.Generator(model, "Fixture", "example.api", "FixtureJNI", ["API.h"], **kwargs).generate()


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
        self.assertIn("Diligent::TestDesc{}", cpp)

    def test_fixed_arguments_share_adapter_configuration(self):
        java, cpp = generate(self.model, adapters={
            "schema_version": 1,
            "fixed_arguments": {"Diligent::EchoTestMode": {"Value": "Diligent::TEST_MODE_LAST"}},
        })
        self.assertIn("EchoTestMode()", java)
        self.assertIn("Diligent::EchoTestMode(Diligent::TEST_MODE_LAST)", cpp)

    def test_rejects_unbounded_numeric_pointer(self):
        declaration(self.model, "SumValues")["parameters"][0]["policy"].pop("length")
        with self.assertRaisesRegex(backend.ModelError, "unsupported pointer"):
            generate(self.model)

    def test_rejects_inout_until_copyback_is_implemented(self):
        declaration(self.model, "CopyDesc")["parameters"][1]["policy"]["direction"] = "inout"
        with self.assertRaisesRegex(backend.ModelError, "inout"):
            generate(self.model)

    def test_rejects_computed_array_count(self):
        policy = declaration(self.model, "EchoValues")["result"]["policy"]
        policy.pop("length")
        policy["length_method"] = "GetCount"
        with self.assertRaisesRegex(backend.ModelError, "computed array lengths"):
            generate(self.model)

    def test_rejects_interface_inheritance_until_pointer_adjustment_is_supported(self):
        interface = declaration(self.model, "ITestCounter")
        base = deepcopy(interface)
        base["name"] = "Diligent::IBaseCounter"
        interface["bases"] = [base["name"]]
        self.model["declarations"].append(base)
        with self.assertRaisesRegex(backend.ModelError, "interface inheritance"):
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
                     "java.lang.AutoCloseable", "java.lang.Override", "java.lang.ref.Cleaner"):
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
