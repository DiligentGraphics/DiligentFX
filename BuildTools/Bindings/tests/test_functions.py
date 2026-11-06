"""Diligent global functions and REF parameters."""

import unittest

from support import extract_fixture
from binding_model import ModelError


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

    def test_selects_public_function_instead_of_cpp_overload(self):
        function = extract_fixture("Functions.h", ["Diligent::MakeBindingTestVector"])["Diligent::MakeBindingTestVector"]
        self.assertEqual(function["result"]["type"]["kind"], "void")
        self.assertEqual([param["name"] for param in function["parameters"]], ["Value", "Result"])
        self.assertEqual(function["parameters"][1]["type"]["kind"], "reference")
        self.assertEqual([param["default"] for param in function["parameters"]], [None, None])
        self.assertIn("C-compatible API", function["documentation"])

    def test_cpp_only_free_function_is_not_exported(self):
        with self.assertRaisesRegex(ModelError, "Declarations not found: Diligent::BindingTestCppOnlyHelper"):
            extract_fixture("Functions.h", ["Diligent::BindingTestCppOnlyHelper"])
