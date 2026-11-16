"""Backend-independent selection and native type helpers on the real fixtures."""

from copy import deepcopy
import unittest

from binding_api import BindingAPI, cpp_type
from binding_model import ModelError
from support import build_fixture, function_config


class BindingAPITest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        config = function_config()
        config["declarations"]["Diligent::IBindingTestMutableObject"] = {}
        config["declarations"]["Diligent::BindingTestExtendedDesc"] = {}
        cls.model = build_fixture(config)

    def test_native_types_and_inherited_members(self):
        api = BindingAPI(self.model)
        methods = api.interface_methods("Diligent::IBindingTestMutableObject")
        self.assertEqual(list(methods), ["GetSize", "Read", "Resize"])
        self.assertEqual(api.scalar(methods["GetSize"]["result"]["type"]), "u64")
        self.assertEqual(api.scalar(methods["Read"]["result"]["type"]), "i32")
        self.assertEqual(cpp_type(methods["Read"]["parameters"][0]["type"]), "const void**")
        self.assertEqual([field["name"] for field in api.record_fields("Diligent::BindingTestExtendedDesc")],
                         ["Size", "pData", "Offset", "Enabled", "Extra"])

    def test_selection_and_overrides_do_not_modify_the_source_model(self):
        original = deepcopy(self.model)
        api = BindingAPI(self.model, {
            "schema_version": 1,
            "native_only": ["Diligent::BindingTestDesc::Enabled"],
            "field_policies": {"Diligent::BindingTestDesc": {"pData": {"nullable": True}}},
        })
        fields = {field["name"]: field for field in api.record_fields("Diligent::BindingTestExtendedDesc")}
        self.assertNotIn("Enabled", fields)
        self.assertTrue(fields["pData"]["policy"]["nullable"])
        self.assertEqual(self.model, original)

    def test_stale_adapter_selections_are_rejected(self):
        with self.assertRaisesRegex(ModelError, "native-only selection"):
            BindingAPI(self.model, {"schema_version": 1, "native_only": ["Diligent::BindingTestDesc::Missing"]})
