"""Evaluated constants, including exact 64-bit values and schema identifiers."""

import json
import unittest

from support import extract_fixture, build_fixture, declarations
from binding_model import ModelError, unalias


class ConstantTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.types = extract_fixture("Constants.h", ["Diligent::BindingTest" + name for name in (
            "InvalidEntity", "Error", "Mask", "Enabled", "Scale", "AutoOffset",
            "PropertyName", "PropertyNamePointer", "Names", "NameArray", "EscapedName", "UnicodeName",
            "JoinedName", "PaddedName", "Schema", "ZeroSchema", "Inherited", "InheritedFlat",
            "InheritedDefault", "InheritedPartial", "InheritedEmptyBase", "InheritedLeaf",
            "InheritedLeafDefault", "InheritedNestedDefault", "InheritedArray")])

    def test_unsigned_64_bit_sentinel_remains_exact(self):
        constant = self.types["Diligent::BindingTestInvalidEntity"]
        self.assertEqual(constant["kind"], "constant")
        self.assertEqual(unalias(constant["type"])["bits"], 64)
        self.assertEqual(constant["value"], 18446744073709551615)
        self.assertEqual(json.loads(json.dumps(constant))["value"], 18446744073709551615)
        self.assertIn("every bit", constant["documentation"])

    def test_native_integer_expressions_and_macros(self):
        self.assertEqual(self.types["Diligent::BindingTestError"]["value"], -7)
        self.assertEqual(self.types["Diligent::BindingTestMask"]["value"], 136)
        self.assertEqual(self.types["Diligent::BindingTestAutoOffset"]["value"], 4294967295)

    def test_bool_and_float_values(self):
        self.assertIs(self.types["Diligent::BindingTestEnabled"]["value"], True)
        self.assertEqual(self.types["Diligent::BindingTestScale"]["value"], 1.5)

    def test_string_value_and_array_type(self):
        constant = self.types["Diligent::BindingTestPropertyName"]
        self.assertEqual(constant["value"], "Translation")
        self.assertEqual(constant["type"]["kind"], "array")
        self.assertEqual(constant["type"]["count"], 12)
        self.assertIn("Canonical property name", constant["documentation"])

    def test_named_string_pointer(self):
        pointer = self.types["Diligent::BindingTestPropertyNamePointer"]
        self.assertEqual(pointer["type"]["kind"], "pointer")
        self.assertTrue(pointer["type"]["const"])
        self.assertEqual(pointer["value"], "Translation")

    def test_grouped_string_references(self):
        # Referenced declarations need not be selected for export. Repeated names
        # have the same value; empty strings and embedded NULs retain their contents.
        self.assertEqual(self.types["Diligent::BindingTestNames"]["value"], {
            "Property": "Translation", "Repeated": "Translation", "Empty": "", "EmbeddedNull": "First\0Second", "Literal": "Literal"})
        self.assertEqual(self.types["Diligent::BindingTestNameArray"]["value"], ["Translation", ""])

    def test_literal_spelling_preserves_string_contents(self):
        self.assertEqual(self.types["Diligent::BindingTestEscapedName"]["value"], 'Line\n"Quoted"\\Path')
        self.assertEqual(self.types["Diligent::BindingTestUnicodeName"]["value"], "\u03c0")
        self.assertEqual(self.types["Diligent::BindingTestJoinedName"]["value"], "BaseColor")
        padded = self.types["Diligent::BindingTestPaddedName"]
        self.assertEqual(padded["type"]["count"], 32)
        self.assertEqual(padded["value"], "Padded")

    def test_rejects_unsupported_pointer_constants(self):
        for name in ("NameOffset", "Address", "MutableNamePointer"):
            with self.subTest(constant=name):
                with self.assertRaisesRegex(ModelError, "unsupported constant initializer"):
                    extract_fixture("Constants.h", ["Diligent::BindingTest" + name])

    def test_uuid_fields_and_nested_array(self):
        constant = self.types["Diligent::BindingTestSchema"]
        self.assertEqual(constant["type"]["name"], "Diligent::BindingTestSchemaID")
        self.assertEqual(constant["value"], {
            "Data1": 0xe4add320, "Data2": 0xeeca, "Data3": 0x439f,
            "Data4": [0xa6, 0xc3, 0x1d, 0x8a, 0x25, 0xae, 0xfb, 0xc3],
        })

    def test_omitted_aggregate_members_are_zero_initialized(self):
        self.assertEqual(self.types["Diligent::BindingTestZeroSchema"]["value"],
                         {"Data1": 0, "Data2": 0, "Data3": 0, "Data4": [0] * 8})

    def test_inherited_aggregate_initializers_follow_base_then_field_order(self):
        for name in ("Inherited", "InheritedFlat"):
            with self.subTest(constant=name):
                self.assertEqual(self.types["Diligent::BindingTest" + name]["value"], {"A": 11, "B": 13})

    def test_inherited_aggregate_defaults_and_partial_initialization(self):
        for name, expected in (("InheritedDefault", {"A": 5, "B": 7}),
                               ("InheritedPartial", {"A": 17, "B": 7}),
                               ("InheritedEmptyBase", {"A": 5, "B": 19})):
            with self.subTest(constant=name):
                self.assertEqual(self.types["Diligent::BindingTest" + name]["value"], expected)

    def test_multilevel_inherited_aggregates(self):
        self.assertEqual(self.types["Diligent::BindingTestInheritedLeaf"]["value"], {"A": 23, "B": 29, "C": 31})
        self.assertEqual(self.types["Diligent::BindingTestInheritedLeafDefault"]["value"], {"A": 5, "B": 7, "C": 0})

    def test_omitted_inherited_records_and_array_elements_keep_base_defaults(self):
        self.assertEqual(self.types["Diligent::BindingTestInheritedNestedDefault"]["value"],
                         {"Child": {"A": 5, "B": 7}})
        self.assertEqual(self.types["Diligent::BindingTestInheritedArray"]["value"],
                         [{"A": 11, "B": 13}, {"A": 5, "B": 7}])

    def test_constants_participate_in_policy_validation(self):
        config = {
            "schema_version": 1,
            "headers": ["tests/fixtures/Constants.h"],
            "declarations": {
                "Diligent::BindingTestInvalidEntity": {},
                "Diligent::BindingTestSchema": {},
                "Diligent::INTERFACE_ID": {"binding": "external", "reason": "Runtime UUID type"},
            },
        }
        model = build_fixture(config)
        self.assertEqual(declarations(model)["Diligent::BindingTestInvalidEntity"]["policy"], {})
