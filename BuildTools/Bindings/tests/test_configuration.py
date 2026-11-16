"""Composition of shared contracts and backend extensions."""

import json
from pathlib import Path
import shutil
import tempfile
import unittest

from binding_model import ModelError
from configuration import load_config


class ConfigurationTest(unittest.TestCase):
    def test_relative_extensions_merge_contracts_without_rebasing_headers(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            base = {
                "schema_version": 1, "headers": ["interface/Public.h"],
                "exports": {"headers": ["interface/*.h"]},
                "declarations": {"Object": {"methods": {"Read": {"result": {"nullable": True}}}}},
            }
            (root / "API.json").write_text(json.dumps(base), encoding="utf-8")
            (root / "backend").mkdir()
            extension = {
                "extends": "../API.json", "headers": ["interface/Public.h", "backend/Native.h"],
                "exports": {"headers": ["backend/Native.h"]},
                "declarations": {"Object": {"methods": {"Read": {"result": {"nullable": False, "lifetime": "call"}}}}},
            }
            path = root / "backend/Model.json"
            path.write_text(json.dumps(extension), encoding="utf-8")
            config = load_config(path)
            self.assertEqual(config["schema_version"], 1)
            self.assertEqual(config["headers"], ["interface/Public.h", "backend/Native.h"])
            self.assertEqual(config["exports"]["headers"], ["interface/*.h", "backend/Native.h"])
            self.assertEqual(config["declarations"]["Object"]["methods"]["Read"]["result"],
                             {"nullable": False, "lifetime": "call"})
            self.assertNotIn("extends", config)
            self.assertEqual(load_config(root / "API.json"), base)

    def test_clang_arguments_preserve_repeated_tokens_and_order(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            base_args = ["-I", "include_a", "-D", "VALUE=1"]
            extension_args = ["-I", "include_b", "-U", "VALUE", "-D", "VALUE=2"]
            final_args = ["-I", "include_a", "-U", "VALUE"]
            (root / "Base.json").write_text(json.dumps({"clang_args": base_args}), encoding="utf-8")
            (root / "Extension.json").write_text(json.dumps({
                "extends": "Base.json", "clang_args": extension_args}), encoding="utf-8")
            path = root / "Final.json"
            path.write_text(json.dumps({"extends": "Extension.json", "clang_args": final_args}), encoding="utf-8")
            self.assertEqual(load_config(path)["clang_args"], base_args + extension_args + final_args)

    def test_extraction_uses_include_paths_from_both_configurations(self):
        from support import TOOLS, build_fixture, declarations, struct_config

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first, second = root / "include_a", root / "include_b"
            first.mkdir()
            second.mkdir()
            # Each include directory is required. Reuse fixtures that include
            # the actual Diligent headers and declare the normal public API.
            shutil.copyfile(TOOLS / "tests/fixtures/Enums.h", first / "Enums.h")
            shutil.copyfile(TOOLS / "tests/fixtures/Structs.h", second / "Structs.h")
            (root / "Public.h").write_text('#include <Enums.h>\n#include <Structs.h>\n', encoding="utf-8")
            base = struct_config()
            base["headers"] = ["Public.h"]
            base["clang_args"] = ["-I", str(first)]
            (root / "API.json").write_text(json.dumps(base), encoding="utf-8")
            path = root / "Model.json"
            path.write_text(json.dumps({
                "extends": "API.json", "clang_args": ["-I", str(second)]}), encoding="utf-8")
            types = declarations(build_fixture(load_config(path), root))
            self.assertEqual([field["name"] for field in types["Diligent::BindingTestDesc"]["fields"]],
                             ["Size", "pData", "Offset", "Enabled"])

    def test_cycles_and_invalid_extensions_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first, second = root / "first.json", root / "second.json"
            first.write_text('{"extends": "second.json"}', encoding="utf-8")
            second.write_text('{"extends": "first.json"}', encoding="utf-8")
            with self.assertRaisesRegex(ModelError, "Cyclic"):
                load_config(first)
            for value in (None, "", [], {}):
                first.write_text(json.dumps({"extends": value}), encoding="utf-8")
                with self.subTest(value=value), self.assertRaisesRegex(ModelError, "base file"):
                    load_config(first)
            first.write_text("[]", encoding="utf-8")
            with self.assertRaisesRegex(ModelError, "must be an object"):
                load_config(first)

    def test_inherited_configuration_is_validated_by_the_model(self):
        from generate import build_model
        from support import TARGET, TOOLS, struct_config, clang_arguments

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            config = struct_config()
            (root / "API.json").write_text(json.dumps(config), encoding="utf-8")
            path = root / "Model.json"
            path.write_text('{"extends": "API.json", "unknown_policy": true}', encoding="utf-8")
            with self.assertRaisesRegex(ModelError, "unknown"):
                build_model(TOOLS, load_config(path), TARGET, clang_arguments())
