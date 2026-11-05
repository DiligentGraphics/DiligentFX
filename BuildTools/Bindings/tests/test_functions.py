"""Diligent global functions and REF parameters."""

import unittest

from support import extract_fixture


class FunctionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.function = extract_fixture("Functions.h", ["Diligent::CreateBindingTestObject"])["Diligent::CreateBindingTestObject"]

    def test_name_result_and_documentation(self):
        self.assertEqual(self.function["kind"], "function")
        self.assertEqual(self.function["name"], "Diligent::CreateBindingTestObject")
        self.assertEqual(self.function["result"]["type"]["name"], "Diligent::BINDING_TEST_STATUS")
        self.assertIn("Creates a reference-counted object", self.function["documentation"])

    def test_ref_expands_to_const_descriptor_reference(self):
        desc = self.function["parameters"][0]
        self.assertEqual(desc["name"], "Desc")
        self.assertEqual(desc["type"]["kind"], "reference")
        self.assertTrue(desc["type"]["target"]["const"])
        self.assertEqual(desc["type"]["target"]["name"], "Diligent::BindingTestDesc")

    def test_interface_output_pointer(self):
        self.assertEqual([param["name"] for param in self.function["parameters"]], ["Desc", "ppObject"])
        pointer = self.function["parameters"][1]["type"]
        self.assertEqual(pointer["kind"], "pointer")
        self.assertEqual(pointer["target"]["kind"], "pointer")
        self.assertEqual(pointer["target"]["target"]["name"], "Diligent::IBindingTestObject")
