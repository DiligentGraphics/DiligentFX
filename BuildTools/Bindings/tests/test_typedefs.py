"""Standalone public typedefs, distinct from C spelling aliases for records."""

import unittest

from support import extract_fixture, build_fixture, struct_config
from binding_model import unalias


class TypedefTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.types = extract_fixture("Typedefs.h", [
            "Diligent::BindingTestEntityID", "Diligent::BindingTestEntityAlias",
            "Diligent::BindingTestSchemaID", "Diligent::BindingTestEventCallback",
            "Diligent::BindingTestDescriptor", "Diligent::BindingTestDesc"])

    def test_scalar_alias_and_documentation(self):
        alias = self.types["Diligent::BindingTestEntityID"]
        self.assertEqual(alias["kind"], "alias")
        self.assertEqual(alias["type"]["name"], "Diligent::Uint64")
        self.assertEqual(unalias(alias["type"])["bits"], 64)
        self.assertIn("Stable test entity identifier", alias["documentation"])

    def test_alias_chain(self):
        alias = self.types["Diligent::BindingTestEntityAlias"]
        self.assertEqual(alias["type"]["name"], "Diligent::BindingTestEntityID")
        self.assertEqual(alias["type"]["target"]["name"], "Diligent::Uint64")
        self.assertEqual(unalias(alias["type"])["bits"], 64)

    def test_record_aliases_do_not_replace_record_definitions(self):
        self.assertEqual(self.types["Diligent::BindingTestDesc"]["kind"], "record")
        for alias_name, record_name in (("BindingTestDescriptor", "BindingTestDesc"),
                                        ("BindingTestSchemaID", "INTERFACE_ID")):
            with self.subTest(alias=alias_name):
                alias = self.types["Diligent::" + alias_name]
                self.assertEqual(alias["kind"], "alias")
                self.assertEqual(unalias(alias["type"])["name"], "Diligent::" + record_name)

    def test_callback_alias(self):
        alias = self.types["Diligent::BindingTestEventCallback"]
        self.assertEqual(alias["kind"], "alias")
        pointer = unalias(alias["type"])
        self.assertEqual(pointer["kind"], "pointer")
        function = pointer["target"]
        self.assertEqual(function["kind"], "function")
        self.assertEqual(function["result"]["kind"], "void")
        self.assertEqual(function["parameters"][0]["name"], "Diligent::BindingTestEntityID")
        self.assertEqual(function["parameters"][1]["kind"], "pointer")

    def test_aliases_participate_in_policy_validation(self):
        config = struct_config()
        config["headers"] = ["tests/fixtures/Typedefs.h"]
        config["declarations"].update({
            "Diligent::BindingTestDescriptor": {},
            "Diligent::BindingTestEventCallback": {"binding": "manual", "reason": "Callback runtime adapter"},
        })
        model = build_fixture(config)
        self.assertEqual(len(model["declarations"]), 3)
