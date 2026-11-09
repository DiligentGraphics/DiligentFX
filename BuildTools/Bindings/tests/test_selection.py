"""Discover public declarations without repeating their names in configuration."""

from copy import deepcopy
import unittest

from binding_model import ModelError
from support import build_fixture, declarations


class HeaderSelectionTest(unittest.TestCase):
    def config(self):
        return {"schema_version": 1, "headers": ["tests/fixtures/selection/Public.h"],
                "exports": {"headers": ["tests/fixtures/selection/*.h"],
                            "exclude_headers": ["tests/fixtures/selection/Dependencies.h"],
                            "exclude": ["Diligent::BindingSelectionExcluded*"]}}

    def test_discovers_constants_aliases_records_enums_and_functions(self):
        config = self.config()
        original = deepcopy(config)
        types = declarations(build_fixture(config))
        self.assertEqual(config, original)
        self.assertEqual({name: d["kind"] for name, d in types.items()}, {
            "Diligent::BindingSelectionIndex": "alias",
            "Diligent::BINDING_SELECTION_MODE": "enum",
            "Diligent::BindingSelectionDesc": "record",
            "Diligent::BindingSelectionDependency": "record",
            "Diligent::BindingSelectionLimit": "constant",
            "Diligent::BindingSelectionRead": "function"})
        self.assertEqual(types["Diligent::BindingSelectionLimit"]["value"], 8)
        # The matching C++ overload, inline helper, templates, and .hpp record
        # are excluded. Native dependencies are included despite header exclusions.
        function = types["Diligent::BindingSelectionRead"]
        self.assertEqual(function["result"]["type"]["kind"], "void")
        self.assertEqual([p["policy"]["direction"] for p in function["parameters"]], ["in", "out"])

    def test_explicit_includes_override_name_and_header_exclusions(self):
        config = self.config()
        config["exports"]["include"] = ["Diligent::BindingSelectionExcludedFirst",
                                        "Diligent::BindingSelectionUnrelated"]
        types = declarations(build_fixture(config))
        self.assertEqual(types["Diligent::BindingSelectionExcludedFirst"]["value"], 10)
        self.assertIn("Diligent::BindingSelectionUnrelated", types)
        self.assertNotIn("Diligent::BindingSelectionExcludedSecond", types)

    def test_symbol_exclusion_does_not_remove_required_types(self):
        config = self.config()
        config["exports"]["exclude"].append("Diligent::BindingSelectionDesc")
        types = declarations(build_fixture(config))
        self.assertIn("Diligent::BindingSelectionDesc", types)

    def test_empty_discovery_and_invalid_selections_are_rejected(self):
        for selection in ({}, {"headers": []}, {"headers": "*.h"},
                          {"headers": [None]}, {"headers": ["*.h", "*.h"]},
                          {"headers": ["*.h"], "exclude": "Diligent::*"},
                          {"headers": ["*.h"], "include": [None]},
                          {"headers": ["*.h"], "unknown": []}):
            with self.subTest(selection=selection), self.assertRaises(ModelError):
                config = self.config()
                config["exports"] = selection
                build_fixture(config)
        config = self.config()
        config["exports"]["headers"] = ["missing/*.h"]
        with self.assertRaisesRegex(ModelError, "No declarations selected"):
            build_fixture(config)


if __name__ == "__main__":
    unittest.main()
