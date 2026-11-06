"""Evaluated constants, including exact 64-bit values and schema identifiers."""

import json
import unittest

from support import extract_fixture, build_fixture, declarations
from binding_model import unalias


class ConstantTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.types = extract_fixture("Constants.h", ["Diligent::BindingTest" + name for name in (
            "InvalidEntity", "Error", "Mask", "Enabled", "Scale", "AutoOffset",
            "PropertyName", "Schema", "ZeroSchema")])

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
