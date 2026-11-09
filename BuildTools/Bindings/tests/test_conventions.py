"""Opt-in API contracts reduce metadata without changing native ownership."""

from copy import deepcopy
import json
import unittest

from binding_model import ModelError, apply_policies, apply_value_policy
from support import build_fixture, declarations, function_config


class ConventionTest(unittest.TestCase):
    def config(self):
        config = function_config()
        config["conventions"] = "diligent"
        config["exports"] = ["Diligent::CreateBindingTestObject"]
        config["declarations"]["Diligent::IObject"]["ownership"] = "reference_counted"
        del config["declarations"]["Diligent::CreateBindingTestObject"]
        config["declarations"]["Diligent::BindingTestDesc"]["fields"]["pData"] = {"direction": "in", "length": "Size"}
        return config

    def test_default_factory_outputs_and_descriptor_storage(self):
        config = self.config()
        original = deepcopy(config)
        model = build_fixture(config)
        self.assertEqual(config, original)
        types = declarations(model)
        parameters = types["Diligent::CreateBindingTestObject"]["parameters"]
        self.assertEqual(parameters[1]["policy"],
                         {"direction": "out", "nullable": False, "ownership": "owned", "lifetime": "caller"})
        self.assertEqual(types["Diligent::BindingTestDesc"]["fields"][1]["policy"],
                         {"direction": "in", "nullable": False, "ownership": "borrowed", "lifetime": "call", "length": "Size"})
        restored = json.loads(json.dumps(model))
        self.assertEqual(apply_policies(restored, config["declarations"], {"Diligent::IID_BindingTestObject"}), model)

    def test_overrides_preserve_nullable_retained_and_borrowed_outputs(self):
        config = self.config()
        config["declarations"]["Diligent::BindingTestDesc"]["fields"]["pData"].update(nullable=True, lifetime="object")
        config["declarations"]["Diligent::CreateBindingTestObject"] = {
            "parameters": {"ppObject": {"ownership": "borrowed"}}}
        types = declarations(build_fixture(config))
        field = types["Diligent::BindingTestDesc"]["fields"][1]
        self.assertTrue(field["policy"]["nullable"])
        self.assertEqual(field["policy"]["lifetime"], "object")
        output = types["Diligent::CreateBindingTestObject"]["parameters"][1]
        self.assertEqual(output["policy"]["ownership"], "borrowed")
        self.assertEqual(output["policy"]["lifetime"], "call")

    def test_interface_returns_default_to_borrowed_and_can_transfer_ownership(self):
        config = self.config()
        config["headers"].append("tests/fixtures/Inference.h")
        config["exports"].append("Diligent::GetBindingTestObject")
        types = declarations(build_fixture(config))
        result = types["Diligent::GetBindingTestObject"]["result"]
        self.assertEqual(result["policy"],
                         {"direction": "out", "nullable": False, "ownership": "borrowed", "lifetime": "object"})
        config["declarations"]["Diligent::GetBindingTestObject"] = {"result": {"ownership": "owned", "nullable": True}}
        result = declarations(build_fixture(config))["Diligent::GetBindingTestObject"]["result"]
        self.assertEqual(result["policy"],
                         {"direction": "out", "nullable": True, "ownership": "owned", "lifetime": "caller"})

    def test_interface_arrays_in_descriptors_do_not_transfer_ownership(self):
        output = declarations(build_fixture(self.config()))["Diligent::CreateBindingTestObject"]["parameters"][1]
        for kind in ("pointer", "reference"):
            value = deepcopy(output)
            value["type"]["kind"] = kind
            apply_value_policy(value, {"direction": "out"}, "Interfaces", is_field=True,
                               interfaces={"Diligent::IBindingTestObject"}, conventions="diligent")
            self.assertEqual(value["policy"]["ownership"], "borrowed")
            self.assertEqual(value["policy"]["lifetime"], "call")
            apply_value_policy(value, {}, "Object", interfaces={"Diligent::IBindingTestObject"}, conventions="diligent")
            self.assertEqual(value["policy"]["ownership"], "owned")

    def test_conventions_require_explicit_opt_in(self):
        config = self.config()
        del config["conventions"]
        with self.assertRaisesRegex(ModelError, "missing pointer metadata"):
            build_fixture(config)
        config["conventions"] = "unknown"
        with self.assertRaisesRegex(ModelError, "Unsupported API conventions"):
            build_fixture(config)


if __name__ == "__main__":
    unittest.main()
