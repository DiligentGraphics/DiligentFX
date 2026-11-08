"""Ownership and metadata validation, separate from API element extraction."""

import copy
import json
import subprocess
import sys
import unittest

import support
from support import declarations, function_config
from binding_model import ModelError, apply_callable_policy, apply_policies
from clang_parser import extract_api


class PolicyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        config = function_config()
        cls.model, cls.symbols = extract_api(
            support.TOOLS, config["headers"], config["declarations"], support.TARGET,
            support.clang_arguments(), external_declarations=["Diligent::IObject"])

    def apply(self, config):
        return apply_policies(copy.deepcopy(self.model), config["declarations"], self.symbols)

    def test_extraction_has_no_pointer_policies(self):
        function = declarations(self.model)["Diligent::CreateBindingTestObject"]
        self.assertNotIn("policy", function["parameters"][1])
        self.assertEqual(function["parameters"][1]["type"]["kind"], "pointer")

    def test_applies_ownership_and_interface_id(self):
        types = declarations(self.apply(function_config()))
        function = types["Diligent::CreateBindingTestObject"]
        self.assertEqual(function["parameters"][0]["policy"], support.BORROWED)
        self.assertEqual(function["parameters"][1]["policy"], support.OUTPUT)
        self.assertEqual(types["Diligent::IBindingTestObject"]["policy"]["iid"], "Diligent::IID_BindingTestObject")
        self.assertEqual(types["Diligent::BINDING_TEST_STATUS"]["policy"]["role"], "status")

    def test_result_array_lengths_name_integer_parameters(self):
        function = copy.deepcopy(declarations(self.model)["Diligent::CreateBindingTestObject"])
        function["parameters"] = [{"name": "Count", "type": {
            "kind": "scalar", "name": "uint", "bits": 32, "const": False, "volatile": False}}]
        function["result"]["type"] = {"kind": "pointer", "const": False, "volatile": False,
            "target": {"kind": "scalar", "name": "float", "bits": 32, "const": True, "volatile": False}}
        policy = {"direction": "out", "nullable": True, "ownership": "borrowed", "lifetime": "static", "length": "Count"}
        apply_callable_policy(function, {"result": policy}, "ArrayResult")
        self.assertEqual(function["result"]["policy"]["length"], "Count")
        for count in [None, "float", "bool", "pointer"]:
            with self.subTest(count=count):
                invalid = copy.deepcopy(function)
                if count is None:
                    invalid["parameters"] = []
                elif count == "pointer":
                    invalid["parameters"][0]["type"] = {"kind": "pointer", "const": False, "volatile": False,
                        "target": function["parameters"][0]["type"]}
                else:
                    invalid["parameters"][0]["type"]["name"] = count
                parameters = {"Count": support.BORROWED} if count == "pointer" else {}
                with self.assertRaisesRegex(ModelError, "ArrayResult result: length must name an integer sibling"):
                    apply_callable_policy(invalid, {"parameters": parameters, "result": policy}, "ArrayResult")

    def test_computed_output_lengths_require_an_input_query(self):
        function = copy.deepcopy(declarations(self.model)["Diligent::CreateBindingTestObject"])
        source_type = function["parameters"][1]["type"]["target"]
        function["parameters"] = [
            {"name": "Source", "type": source_type},
            {"name": "pValues", "type": {
                "kind": "pointer", "const": False, "volatile": False,
                "target": {"kind": "scalar", "name": "float", "bits": 32,
                           "const": False, "volatile": False}}},
        ]
        policies = {"parameters": {
            "Source": {"direction": "in", "nullable": False, "ownership": "borrowed", "lifetime": "call"},
            "pValues": {"direction": "out", "nullable": False, "ownership": "borrowed", "lifetime": "call",
                        "length_from": {"parameter": "Source", "method": "GetDesc", "field": "Count"}},
        }}
        apply_callable_policy(function, policies, "ComputedOutput")
        self.assertEqual(function["parameters"][1]["policy"], policies["parameters"]["pValues"])
        for query in (None, "Source", {}, {"parameter": "Source"},
                      {"parameter": "Source", "method": ""},
                      {"parameter": "Source", "method": "Count", "field": 1},
                      {"parameter": "missing", "method": "Count"},
                      {"parameter": "pValues", "method": "Count"}):
            with self.subTest(query=query):
                invalid = copy.deepcopy(policies)
                invalid["parameters"]["pValues"]["length_from"] = query
                with self.assertRaisesRegex(ModelError, "length_from"):
                    apply_callable_policy(copy.deepcopy(function), invalid, "ComputedOutput")
        for key, value in (("length", "Source"), ("length_method", "GetCount"),
                           ("written", "Written"), ("nullable", True), ("direction", "in")):
            with self.subTest(key=key):
                invalid = copy.deepcopy(policies)
                invalid["parameters"]["pValues"][key] = value
                with self.assertRaisesRegex(ModelError, "length_from"):
                    apply_callable_policy(copy.deepcopy(function), invalid, "ComputedOutput")
        invalid = copy.deepcopy(policies)
        invalid["parameters"]["Source"]["nullable"] = True
        with self.assertRaisesRegex(ModelError, "non-null borrowed input"):
            apply_callable_policy(copy.deepcopy(function), invalid, "ComputedOutput")

    def test_requires_pointer_semantics(self):
        config = function_config()
        del config["declarations"]["Diligent::CreateBindingTestObject"]["parameters"]["ppObject"]
        with self.assertRaisesRegex(ModelError, "CreateBindingTestObject::ppObject: missing pointer metadata"):
            self.apply(config)

    def test_const_return_values_are_outputs_without_writable_storage(self):
        function = copy.deepcopy(declarations(self.model)["Diligent::CreateBindingTestObject"])
        # Reuse an extracted const-reference type. Returning const data is valid;
        # passing the same type as writable output storage must still be rejected.
        function["result"]["type"] = function["parameters"][0]["type"]
        function["parameters"] = []
        policy = {"direction": "out", "nullable": False, "ownership": "borrowed", "lifetime": "object"}
        apply_callable_policy(function, {"result": policy}, "ConstResult")
        self.assertEqual(function["result"]["policy"], policy)
        function["parameters"] = [{"name": "Output", "type": function["result"]["type"]}]
        with self.assertRaisesRegex(ModelError, "output cannot point to const"):
            apply_callable_policy(function, {"result": policy, "parameters": {"Output": policy}}, "ConstResult")

    def test_const_pointer_fields_can_describe_returned_data(self):
        for direction in ("out", "inout"):
            with self.subTest(direction=direction):
                config = function_config()
                config["declarations"]["Diligent::BindingTestDesc"]["fields"]["pData"]["direction"] = direction
                fields = declarations(self.apply(config))["Diligent::BindingTestDesc"]["fields"]
                self.assertEqual(fields[1]["policy"]["direction"], direction)
                self.assertTrue(fields[1]["type"]["target"]["const"])

    def test_rejects_stale_or_inconsistent_metadata(self):
        cases = [
            (lambda c: c["declarations"].update({"Diligent::BindingTestDesc": None}), "expected a metadata object"),
            (lambda c: c["declarations"]["Diligent::BindingTestDesc"]["fields"].update({"Missing": {}}), "members: unknown metadata"),
            (lambda c: c["declarations"]["Diligent::IBindingTestObject"]["methods"].update({"Missing": {}}), "methods: unknown metadata"),
            (lambda c: c["declarations"]["Diligent::BindingTestDesc"]["fields"]["pData"].update(length="Missing"), "length must name"),
            (lambda c: c["declarations"]["Diligent::BindingTestDesc"]["fields"]["pData"].update(unsupported_key="borrowed"), "unknown metadata"),
            (lambda c: c["declarations"]["Diligent::IBindingTestObject"].update(iid="Diligent::Missing"), "interface ID not found"),
            (lambda c: c["declarations"]["Diligent::IObject"].pop("reason"), "requires a reason"),
        ]
        for mutate, message in cases:
            with self.subTest(message=message):
                config = function_config()
                mutate(config)
                with self.assertRaisesRegex(ModelError, message):
                    self.apply(config)

    def test_rejects_incomplete_serialized_models(self):
        config = function_config()
        del config["declarations"]["Diligent::IObject"]
        model = copy.deepcopy(self.model)
        model["declarations"] = [decl for decl in model["declarations"] if decl["name"] != "Diligent::IObject"]
        with self.assertRaisesRegex(ModelError, "referenced types are missing from the model: Diligent::IObject"):
            apply_policies(model, config["declarations"], self.symbols)

    def test_manual_adapter_is_explicit(self):
        config = function_config()
        config["declarations"]["Diligent::BindingTestDesc"]["fields"]["pData"] = {
            "binding": "manual", "reason": "Scoped storage adapter", "length": "Size"}
        fields = declarations(self.apply(config))["Diligent::BindingTestDesc"]["fields"]
        self.assertEqual(fields[1]["policy"]["binding"], "manual")

    def test_serialized_model_can_be_validated_without_clang(self):
        # -S excludes installed packages. Model consumers do not need Clang.
        script = """
import json
import sys
sys.path.insert(0, sys.argv[1])
from binding_model import apply_policies
data = json.load(sys.stdin)
model = apply_policies(data['model'], data['policies'], data['symbols'])
assert 'clang' not in sys.modules
assert 'clang_parser' not in sys.modules
print(json.dumps(model))
"""
        data = {"model": self.model, "policies": function_config()["declarations"], "symbols": sorted(self.symbols)}
        result = subprocess.run(
            [sys.executable, "-B", "-S", "-c", script, str(support.TOOLS)],
            input=json.dumps(data), capture_output=True, text=True, check=True)
        validated = declarations(json.loads(result.stdout))
        self.assertEqual(validated["Diligent::CreateBindingTestObject"]["parameters"][1]["policy"], support.OUTPUT)
