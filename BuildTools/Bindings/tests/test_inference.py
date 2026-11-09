"""Dependency discovery and minimal semantic overrides over real C API headers."""

from copy import deepcopy
import unittest

from binding_model import ModelError, apply_policies, apply_value_policy, unalias
from support import build_fixture, declarations, function_config


class InferenceTest(unittest.TestCase):
    def function_config(self):
        config = function_config()
        config["exports"] = ["Diligent::CreateBindingTestObject"]
        config["declarations"]["Diligent::IObject"]["ownership"] = "reference_counted"
        del config["declarations"]["Diligent::IBindingTestObject"]["iid"]
        del config["declarations"]["Diligent::CreateBindingTestObject"]["parameters"]["Desc"]
        return config

    def test_factory_discovers_types_and_infers_interface_contract(self):
        config = self.function_config()
        original = deepcopy(config)
        types = declarations(build_fixture(config))
        self.assertEqual(config, original)
        self.assertEqual(set(types), {"Diligent::CreateBindingTestObject", "Diligent::IBindingTestObject",
                                    "Diligent::IObject", "Diligent::BindingTestDesc", "Diligent::BINDING_TEST_STATUS"})
        interface = types["Diligent::IBindingTestObject"]
        self.assertEqual(interface["policy"]["iid"], "Diligent::IID_BindingTestObject")
        self.assertEqual(interface["policy"]["ownership"], "reference_counted")
        self.assertEqual(types["Diligent::CreateBindingTestObject"]["parameters"][0]["policy"],
                         {"direction": "in", "nullable": False, "ownership": "borrowed", "lifetime": "call"})

    def test_inherited_ownership_does_not_invent_an_interface_id(self):
        config = {"schema_version": 1, "headers": ["tests/fixtures/Interfaces.h"],
                  "exports": ["Diligent::IBindingTestMutableObject"],
                  "declarations": {
                      "Diligent::IObject": {"binding": "external", "reason": "Runtime base", "ownership": "reference_counted"},
                      "Diligent::IBindingTestObject": {"include_methods": ["GetSize"]},
                  }}
        types = declarations(build_fixture(config))
        derived = types["Diligent::IBindingTestMutableObject"]
        self.assertEqual(derived["policy"]["ownership"], "reference_counted")
        self.assertNotIn("iid", derived["policy"])
        self.assertEqual(types["Diligent::IBindingTestObject"]["policy"]["iid"], "Diligent::IID_BindingTestObject")

    def test_retained_const_reference_keeps_its_explicit_lifetime(self):
        config = self.function_config()
        config["declarations"]["Diligent::CreateBindingTestObject"]["parameters"]["Desc"] = {"lifetime": "object"}
        types = declarations(build_fixture(config))
        self.assertEqual(types["Diligent::CreateBindingTestObject"]["parameters"][0]["policy"],
                         {"direction": "in", "nullable": False, "ownership": "borrowed", "lifetime": "object"})

    def test_nested_arrays_aliases_bases_and_recursive_pointers(self):
        pointer = {"direction": "in", "nullable": True, "ownership": "borrowed", "lifetime": "call"}
        config = {
            "schema_version": 1,
            "headers": ["tests/fixtures/Inference.h"],
            "exports": ["Diligent::UseBindingTestEnvelope"],
            "declarations": {
                "Diligent::BindingTestDesc": {"fields": {"pData": dict(pointer, length="Size")}},
                "Diligent::BindingTestEnvelope": {"fields": {"pLink": pointer}},
                "Diligent::BindingTestLink": {"fields": {"pNext": pointer}},
            },
        }
        types = declarations(build_fixture(config))
        self.assertEqual(set(types), {"Diligent::UseBindingTestEnvelope", "Diligent::BindingTestEnvelope",
                                     "Diligent::BindingTestExtendedDesc", "Diligent::BindingTestDesc",
                                     "Diligent::BindingTestLink", "Diligent::BindingTestVector"})
        alias = types["Diligent::UseBindingTestEnvelope"]["parameters"][0]["type"]["target"]
        self.assertEqual(alias["name"], "Diligent::BindingTestEnvelopeAlias")
        self.assertEqual(unalias(alias)["name"], "Diligent::BindingTestEnvelope")
        self.assertEqual(types["Diligent::BindingTestEnvelope"]["fields"][0]["type"]["count"], 2)
        self.assertEqual(types["Diligent::BindingTestLink"]["fields"][0]["type"]["target"]["name"],
                         "Diligent::BindingTestLink")

    def test_result_reference_needs_no_policy(self):
        config = {"schema_version": 1, "headers": ["tests/fixtures/Inference.h"],
                  "exports": ["Diligent::GetBindingTestVector"]}
        types = declarations(build_fixture(config))
        self.assertEqual(types["Diligent::GetBindingTestVector"]["result"]["policy"],
                         {"direction": "out", "nullable": False, "ownership": "borrowed", "lifetime": "call"})

    def test_mutable_reference_defaults_to_output_and_honors_overrides(self):
        config = {"schema_version": 1, "headers": ["tests/fixtures/Inference.h"],
                  "exports": ["Diligent::EditBindingTestVector"]}
        function = declarations(build_fixture(config))["Diligent::EditBindingTestVector"]
        self.assertEqual(function["parameters"][0]["policy"],
                         {"direction": "out", "nullable": False, "ownership": "borrowed", "lifetime": "call"})
        for direction in ("in", "inout"):
            with self.subTest(direction=direction):
                config["declarations"] = {"Diligent::EditBindingTestVector": {
                    "parameters": {"Value": {"direction": direction, "lifetime": "object"}}}}
                function = declarations(build_fixture(config))["Diligent::EditBindingTestVector"]
                self.assertEqual(function["parameters"][0]["policy"]["lifetime"], "object")
                self.assertEqual(function["parameters"][0]["policy"]["direction"], direction)

    def direction_config(self):
        borrowed = {"nullable": False, "ownership": "borrowed", "lifetime": "call"}
        parameters = {name: borrowed.copy() for name in (
            "pInput", "pOutput", "pConstPointer", "ppText", "Text", "ppInputText",
            "pAliasedInput", "pAliasedText", "pObject", "pExternal", "pContext")}
        parameters["ppObject"] = {"nullable": False, "ownership": "owned", "lifetime": "caller"}
        return {"schema_version": 1, "headers": ["tests/fixtures/Inference.h"],
                "exports": ["Diligent::InferBindingTestDirections", "Diligent::IBindingTestObject"],
                "declarations": {
                    "Diligent::IObject": {"binding": "external", "reason": "Runtime base",
                                          "ownership": "reference_counted"},
                    "Diligent::IBindingTestObject": {"methods": {
                        "Read": {"parameters": {"ppData": dict(borrowed, lifetime="read_scope")}}}},
                    "Diligent::InferBindingTestDirections": {"parameters": parameters}}}

    def test_pointer_levels_aliases_and_interface_directions(self):
        config = self.direction_config()
        model = build_fixture(config)
        types = declarations(model)
        parameters = types["Diligent::InferBindingTestDirections"]["parameters"]
        self.assertEqual({p["name"]: p["policy"]["direction"] for p in parameters}, {
            "pInput": "in", "pOutput": "out", "pConstPointer": "out",
            "ppText": "out", "Text": "out", "ppInputText": "in",
            "pAliasedInput": "in", "pAliasedText": "out",
            "pObject": "in", "Object": "in", "ConstObject": "in", "ppObject": "out",
            "pExternal": "in", "External": "in", "pContext": "out"})
        methods = {m["name"]: m for m in types["Diligent::IBindingTestObject"]["methods"]}
        self.assertEqual(methods["Read"]["parameters"][0]["policy"]["direction"], "out")

        # Deserialized declarations need not be ordered before their users.
        reordered = deepcopy(model)
        reordered["declarations"].reverse()
        self.assertEqual(declarations(apply_policies(reordered, config["declarations"],
                                                    {"Diligent::IID_BindingTestObject"})), types)

    def test_mutable_pointer_input_override(self):
        config = self.direction_config()
        config["declarations"]["Diligent::InferBindingTestDirections"]["parameters"]["pContext"]["direction"] = "in"
        parameters = declarations(build_fixture(config))["Diligent::InferBindingTestDirections"]["parameters"]
        self.assertEqual(parameters[-1]["policy"]["direction"], "in")

    def test_const_alias_rejects_output_override(self):
        config = self.direction_config()
        config["declarations"]["Diligent::InferBindingTestDirections"]["parameters"]["pAliasedInput"]["direction"] = "out"
        with self.assertRaisesRegex(ModelError, "output cannot point to const data"):
            build_fixture(config)

    def test_pointer_ownership_and_lifetime_are_not_guessed(self):
        config = self.function_config()
        del config["declarations"]["Diligent::CreateBindingTestObject"]["parameters"]["ppObject"]
        with self.assertRaisesRegex(ModelError, "ppObject: missing pointer metadata"):
            build_fixture(config)
        # A null field default does not make null input valid. Required native
        # resource pointers also commonly default to null in C++ descriptors.
        value = {"type": {"kind": "pointer", "const": False, "volatile": False,
                          "target": {"kind": "scalar", "name": "char_s", "bits": 8, "const": True, "volatile": False}},
                 "default": "nullptr"}
        with self.assertRaisesRegex(ModelError, "missing pointer metadata"):
            apply_value_policy(value, {}, "Name", is_field=True)
        # A const pointer field can also appear in output or bidirectional
        # descriptors, so its direction is not inferred from constness.
        with self.assertRaisesRegex(ModelError, "missing pointer metadata: direction"):
            apply_value_policy(value, {"nullable": True, "ownership": "borrowed", "lifetime": "call"},
                               "Name", is_field=True)

    def test_partial_method_selection_and_external_types_stop_traversal(self):
        config = self.function_config()
        config["declarations"]["Diligent::IBindingTestObject"] = {"include_methods": ["GetSize"]}
        types = declarations(build_fixture(config))
        self.assertEqual([m["name"] for m in types["Diligent::IBindingTestObject"]["methods"]], ["GetSize"])
        self.assertEqual(types["Diligent::IObject"]["kind"], "external")
        self.assertNotIn("Diligent::IReferenceCounters", types)
        self.assertNotIn("Diligent::IBindingTestMutableObject", types)

    def test_unreachable_overrides_and_invalid_exports_are_rejected(self):
        config = self.function_config()
        config["declarations"]["Diligent::BindingTestVector"] = {}
        with self.assertRaisesRegex(ModelError, "outside the selected API"):
            build_fixture(config)
        for exports in ([], "Diligent::CreateBindingTestObject", [None], ["a", "a"]):
            with self.subTest(exports=exports), self.assertRaisesRegex(ModelError, "exports must be"):
                config["exports"] = exports
                build_fixture(config)
