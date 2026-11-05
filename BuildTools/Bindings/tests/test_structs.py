"""Diligent descriptors, aliases, defaults, arrays, and C++ helpers."""

import unittest

from support import extract_fixture
from binding_model import unalias


class StructTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.types = extract_fixture("Structs.h", [
            "Diligent::BindingTestDesc", "Diligent::BindingTestExtendedDesc",
            "Diligent::BindingTestVector", "Diligent::BindingTestCallbackDesc"])
        cls.fields = {field["name"]: field for field in cls.types["Diligent::BindingTestDesc"]["fields"]}

    def test_field_order_defaults_and_documentation(self):
        desc = self.types["Diligent::BindingTestDesc"]
        self.assertEqual(desc["kind"], "record")
        self.assertEqual([field["name"] for field in desc["fields"]], ["Size", "pData", "Offset", "Enabled"])
        self.assertEqual([field["default"] for field in desc["fields"]], ["0", "nullptr", "{ 1.f , 2.f }", "False"])
        self.assertIn("Descriptor documentation", desc["documentation"])
        self.assertIn("Number of input bytes", self.fields["Size"]["documentation"])

    def test_diligent_aliases_and_const_pointer(self):
        size = self.fields["Size"]["type"]
        self.assertEqual(size["name"], "Diligent::BindingTestSize")
        self.assertEqual(size["target"]["name"], "Diligent::Uint64")
        self.assertEqual(unalias(size)["bits"], 64)
        pointer = self.fields["pData"]["type"]
        self.assertEqual(pointer["kind"], "pointer")
        self.assertEqual(pointer["target"]["kind"], "void")
        self.assertTrue(pointer["target"]["const"])
        self.assertEqual(unalias(self.fields["Enabled"]["type"])["name"], "bool")

    def test_fixed_array_type(self):
        array = self.fields["Offset"]["type"]
        self.assertEqual((array["kind"], array["count"]), ("array", 2))
        self.assertEqual(array["element"]["name"], "Diligent::Float32")
        self.assertEqual(unalias(array["element"])["bits"], 32)

    def test_derived_descriptor_remains_a_record(self):
        desc = self.types["Diligent::BindingTestExtendedDesc"]
        self.assertEqual(desc["kind"], "record")
        self.assertEqual(desc["bases"], ["Diligent::BindingTestDesc"])
        self.assertEqual([field["name"] for field in desc["fields"]], ["Extra"])
        self.assertEqual(desc["fields"][0]["default"], "7")
        self.assertEqual(desc["methods"], [])

    def test_omits_cpp_constructors_and_operators(self):
        vector = self.types["Diligent::BindingTestVector"]
        self.assertEqual(vector["kind"], "record")
        self.assertEqual(vector["methods"], [])
        self.assertEqual([field["name"] for field in vector["fields"]], ["Values"])
        self.assertEqual(vector["fields"][0]["default"], "{ 0.f , 0.f }")

    def test_callback_typedef(self):
        callback = self.types["Diligent::BindingTestCallbackDesc"]["fields"][0]
        self.assertEqual(callback["default"], "nullptr")
        self.assertEqual(callback["type"]["name"], "Diligent::BindingTestCallbackType")
        pointer = unalias(callback["type"])
        self.assertEqual(pointer["kind"], "pointer")
        function = pointer["target"]
        self.assertEqual(function["kind"], "function")
        self.assertEqual(function["result"]["kind"], "void")
        self.assertEqual(unalias(function["parameters"][0])["bits"], 64)
        self.assertEqual(function["parameters"][1]["kind"], "pointer")
        self.assertEqual(function["parameters"][1]["target"]["kind"], "void")
