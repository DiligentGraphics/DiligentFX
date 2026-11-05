"""Diligent typed enums, implicit values, and flag combinations."""

import unittest

from support import extract_fixture
from binding_model import unalias


class EnumTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.types = extract_fixture("Enums.h", [
            "Diligent::BINDING_TEST_STATUS", "Diligent::BINDING_TEST_MODE", "Diligent::BINDING_TEST_FLAGS"])

    def test_signed_status_values_and_documentation(self):
        status = self.types["Diligent::BINDING_TEST_STATUS"]
        self.assertEqual(status["kind"], "enum")
        underlying = unalias(status["underlying_type"])
        self.assertEqual((underlying["name"], underlying["bits"]), ("int", 32))
        self.assertEqual([(value["name"], value["value"]) for value in status["values"]], [
            ("BINDING_TEST_STATUS_FAILED", -1), ("BINDING_TEST_STATUS_OK", 0), ("BINDING_TEST_STATUS_PENDING", 3)])
        self.assertIn("Status returned by the test API", status["documentation"])
        self.assertIn("The operation failed", status["values"][0]["documentation"])

    def test_eight_bit_enum_with_implicit_value(self):
        mode = self.types["Diligent::BINDING_TEST_MODE"]
        underlying = unalias(mode["underlying_type"])
        self.assertEqual((underlying["name"], underlying["bits"]), ("uchar", 8))
        self.assertEqual([value["value"] for value in mode["values"]], [0, 1])

    def test_flag_expressions(self):
        flags = self.types["Diligent::BINDING_TEST_FLAGS"]
        underlying = unalias(flags["underlying_type"])
        self.assertEqual((underlying["name"], underlying["bits"]), ("uint", 32))
        self.assertEqual([value["value"] for value in flags["values"]], [0, 1, 2, 3])
