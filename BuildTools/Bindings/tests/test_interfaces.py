"""Real IObject inheritance and Diligent interface macro expansion."""

import unittest

from support import extract_fixture, build_fixture, declarations
from binding_model import ModelError, unalias


class InterfaceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.types = extract_fixture("Interfaces.h", [
            "Diligent::IObject", "Diligent::IBindingTestObject", "Diligent::IBindingTestMutableObject"])

    def selection_config(self, methods):
        return {
            "schema_version": 1,
            "headers": ["tests/fixtures/Interfaces.h"],
            "declarations": {
                "Diligent::IObject": {"binding": "external", "reason": "Runtime supplies reference counting"},
                "Diligent::IBindingTestObject": {"include_methods": methods},
            },
        }

    def test_selected_methods_do_not_require_omitted_types_or_policies(self):
        # Read() requires a status enum and output-pointer policy. Neither is
        # required when only GetSize() is selected.
        model = declarations(build_fixture(self.selection_config(["GetSize"])))
        self.assertEqual([m["name"] for m in model["Diligent::IBindingTestObject"]["methods"]], ["GetSize"])
        self.assertNotIn("Diligent::BINDING_TEST_STATUS", model)

    def test_empty_selection_preserves_interface_identity_and_base(self):
        model = declarations(build_fixture(self.selection_config([])))
        interface = model["Diligent::IBindingTestObject"]
        self.assertEqual(interface["kind"], "interface")
        self.assertEqual(interface["bases"], ["Diligent::IObject"])
        self.assertEqual(interface["methods"], [])

    def test_invalid_method_selections_are_rejected(self):
        for selection in ("GetSize", ["GetSize", "GetSize"], [None], ["Missing"], ["AddRef"]):
            with self.subTest(selection=selection), self.assertRaises(ModelError):
                build_fixture(self.selection_config(selection))
        config = self.selection_config(["GetSize"])
        config["declarations"]["Diligent::IBindingTestObject"]["methods"] = {"Read": {}}
        with self.assertRaisesRegex(ModelError, "unknown metadata"):
            build_fixture(config)

    def exclusion_config(self, methods):
        config = self.selection_config([])
        config["declarations"]["Diligent::IBindingTestObject"] = {"exclude_methods": methods}
        return config

    def test_excluded_methods_do_not_require_dependencies_or_policies(self):
        model = declarations(build_fixture(self.exclusion_config(["Read"])))
        self.assertEqual([m["name"] for m in model["Diligent::IBindingTestObject"]["methods"]], ["GetSize"])
        self.assertNotIn("Diligent::BINDING_TEST_STATUS", model)
        model = declarations(build_fixture(self.exclusion_config(["Read", "GetSize"])))
        self.assertEqual(model["Diligent::IBindingTestObject"]["kind"], "interface")
        self.assertEqual(model["Diligent::IBindingTestObject"]["methods"], [])

    def test_unlisted_and_derived_methods_are_included_automatically(self):
        config = self.exclusion_config(["Read"])
        config["exports"] = ["Diligent::IBindingTestMutableObject"]
        model = declarations(build_fixture(config))
        self.assertEqual([m["name"] for m in model["Diligent::IBindingTestObject"]["methods"]], ["GetSize"])
        self.assertEqual([m["name"] for m in model["Diligent::IBindingTestMutableObject"]["methods"]], ["Resize"])
        config = self.exclusion_config([])
        config["conventions"] = "diligent"
        model = declarations(build_fixture(config))
        self.assertEqual([m["name"] for m in model["Diligent::IBindingTestObject"]["methods"]], ["GetSize", "Read"])

    def test_invalid_method_exclusions_are_rejected(self):
        for exclusion in ("Read", ["Read", "Read"], [None], ["Missing"], ["AddRef"]):
            with self.subTest(exclusion=exclusion), self.assertRaises(ModelError):
                build_fixture(self.exclusion_config(exclusion))
        config = self.exclusion_config(["Read"])
        config["declarations"]["Diligent::IBindingTestObject"]["include_methods"] = ["GetSize"]
        with self.assertRaisesRegex(ModelError, "mutually exclusive"):
            build_fixture(config)
        del config["declarations"]["Diligent::IBindingTestObject"]["include_methods"]
        config["declarations"]["Diligent::IBindingTestObject"]["methods"] = {"Read": {}}
        with self.assertRaisesRegex(ModelError, "unknown metadata"):
            build_fixture(config)

    def test_iobject_inheritance_and_method_order(self):
        interface = self.types["Diligent::IBindingTestObject"]
        self.assertEqual(interface["kind"], "interface")
        self.assertEqual(interface["bases"], ["Diligent::IObject"])
        self.assertEqual(interface["fields"], [])
        self.assertEqual([method["name"] for method in interface["methods"]], ["GetSize", "Read"])
        self.assertIn("Reference-counted test interface", interface["documentation"])

    def test_const_no_argument_method(self):
        method = self.types["Diligent::IBindingTestObject"]["methods"][0]
        self.assertTrue(method["virtual"])
        self.assertTrue(method["const"])
        self.assertEqual(method["parameters"], [])
        self.assertEqual(unalias(method["result"]["type"])["bits"], 64)
        self.assertIn("Returns the byte count", method["documentation"])

    def test_output_pointer_method(self):
        method = self.types["Diligent::IBindingTestObject"]["methods"][1]
        self.assertTrue(method["virtual"])
        self.assertFalse(method["const"])
        self.assertEqual(method["result"]["type"]["name"], "Diligent::BINDING_TEST_STATUS")
        self.assertEqual([param["name"] for param in method["parameters"]], ["ppData"])
        pointer = method["parameters"][0]["type"]
        self.assertEqual(pointer["kind"], "pointer")
        self.assertEqual(pointer["target"]["kind"], "pointer")
        self.assertTrue(pointer["target"]["target"]["const"])

    def test_derived_interface(self):
        interface = self.types["Diligent::IBindingTestMutableObject"]
        self.assertEqual(interface["kind"], "interface")
        self.assertEqual(interface["bases"], ["Diligent::IBindingTestObject"])
        self.assertEqual([method["name"] for method in interface["methods"]], ["Resize"])
        size = interface["methods"][0]["parameters"][0]
        self.assertEqual(size["name"], "Size")
        self.assertEqual(unalias(size["type"])["bits"], 64)

    def test_omits_real_iobject_template_helper(self):
        methods = self.types["Diligent::IObject"]["methods"]
        self.assertEqual([method["name"] for method in methods],
                         ["QueryInterface", "AddRef", "Release", "GetReferenceCounters"])
        self.assertTrue(all(method["virtual"] for method in methods))

    def test_default_arguments(self):
        method = self.types["Diligent::IBindingTestMutableObject"]["methods"][0]
        self.assertEqual([param["name"] for param in method["parameters"]], ["Size", "PreserveData", "UpdateState"])
        self.assertEqual([param["default"] for param in method["parameters"]], [None, "True", "False"])
