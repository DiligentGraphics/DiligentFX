"""Descriptor directions follow the selected API, including nested storage."""

from copy import deepcopy
import unittest

from binding_model import apply_policies, record_usage
from support import build_fixture, declarations


class FieldUsageTest(unittest.TestCase):
    def config(self, names):
        return {"schema_version": 1, "conventions": "diligent",
                "headers": ["tests/fixtures/Inference.h"],
                "exports": ["Diligent::" + name for name in names],
                "declarations": {"Diligent::BindingTestDesc": {"fields": {"pData": {"length": "Size"}}}}}

    def assert_directions(self, model, expected):
        types = declarations(model)
        for name in ("BindingTestDesc", "BindingTestLink", "BindingTestEnvelope"):
            for field in types["Diligent::" + name]["fields"]:
                if field["type"]["kind"] == "pointer":
                    self.assertEqual(field["policy"]["direction"], expected, name + "::" + field["name"])

    def test_nested_bases_aliases_fixed_arrays_and_recursive_pointers(self):
        for names, direction in ((["UseBindingTestEnvelope"], "in"),
                                 (["GetBindingTestEnvelope"], "out"),
                                 (["GetBindingTestEnvelopeSnapshot"], "out"),
                                 (["UseBindingTestEnvelope", "GetBindingTestEnvelope"], "inout"),
                                 (["BindingTestEnvelope"], "in")):
            with self.subTest(names=names):
                config = self.config(names)
                model = build_fixture(config)
                self.assert_directions(model, direction)
                reordered = deepcopy(model)
                reordered["declarations"].reverse()
                self.assert_directions(apply_policies(reordered, config["declarations"], set()), direction)

    def test_inout_parameter_contributes_both_record_uses(self):
        config = self.config(["GetBindingTestEnvelope"])
        config["declarations"]["Diligent::GetBindingTestEnvelope"] = {"parameters": {"Desc": {"direction": "inout"}}}
        model = build_fixture(config)
        self.assert_directions(model, "inout")
        inputs, outputs, storage = record_usage(declarations(model))
        for used in (inputs, outputs, storage):
            self.assertIn("Diligent::BindingTestEnvelope", used)

    def test_explicit_field_direction_overrides_descriptor_use(self):
        config = self.config(["UseBindingTestEnvelope"])
        config["declarations"]["Diligent::BindingTestDesc"]["fields"]["pData"]["direction"] = "out"
        types = declarations(build_fixture(config))
        field = types["Diligent::BindingTestDesc"]["fields"][1]
        self.assertEqual(field["policy"]["direction"], "out")
        self.assertEqual(types["Diligent::BindingTestEnvelope"]["fields"][-1]["policy"]["direction"], "in")


if __name__ == "__main__":
    unittest.main()
