"""Integration tests for Radient's selected public binding API."""

import argparse
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "BuildTools/Bindings"))
from binding_model import unalias
from generate import build_model


API_TARGET = "x86_64-pc-windows-msvc" if sys.platform == "win32" else "x86_64-unknown-linux-gnu"
CLANG_ARGS = []


def declarations(model):
    return {declaration["name"]: declaration for declaration in model["declarations"]}


class PublicAPITest(unittest.TestCase):
    def test_data_blob_contract(self):
        config = json.loads((ROOT / "Radient/bindings/DataBlob.json").read_text(encoding="utf-8"))
        self.assertTrue(all(Path(header).suffix == ".h" for header in config["headers"]))
        model = build_model(ROOT, config, API_TARGET, CLANG_ARGS)
        self.assertTrue(all(Path(declaration["source"]["file"]).suffix == ".h"
                            for declaration in model["declarations"]))
        types = declarations(model)
        self.assertEqual(len(types), 8)
        blob = types["Diligent::IRadientDataBlob"]
        mutable = types["Diligent::IRadientMutableDataBlob"]
        self.assertEqual(blob["bases"], ["Diligent::IObject"])
        self.assertEqual(mutable["bases"], ["Diligent::IRadientDataBlob"])
        self.assertEqual([method["name"] for method in mutable["methods"]], ["BeginWrite", "EndWrite", "Resize"])
        self.assertEqual(unalias(blob["methods"][0]["result"]["type"])["bits"], 64)
        info = types["Diligent::RadientDataBlobCreateInfo"]
        self.assertEqual([field["default"] for field in info["fields"]], ["0", "nullptr", "nullptr", "nullptr", "nullptr"])
        callback = unalias(info["fields"][2]["type"])["target"]
        self.assertEqual(callback["kind"], "function")
        self.assertEqual(callback["parameters"][0]["target"]["kind"], "alias")
        create = types["Diligent::CreateRadientDataBlob"]
        self.assertEqual(create["parameters"][-1]["policy"]["ownership"], "owned")
        self.assertEqual(blob["methods"][1]["policy"]["binding"], "manual")
        self.assertEqual(types["Diligent::RADIENT_STATUS"]["values"][-1]["value"], -7)

    def test_extractor_preserves_data_and_omits_cpp_helpers(self):
        config = {
            "schema_version": 1,
            "headers": ["Radient/interface/RadientTypes.h"],
            "declarations": {
                "Diligent::RadientMatrix4x4": {},
                "Diligent::RadientAssetReference": {
                    "fields": {
                        "URI": {"direction": "in", "nullable": True,
                                "ownership": "borrowed", "lifetime": "call"}
                    }
                },
            },
        }
        types = declarations(build_model(ROOT, config, API_TARGET, CLANG_ARGS))
        matrix = types["Diligent::RadientMatrix4x4"]
        reference = types["Diligent::RadientAssetReference"]
        self.assertEqual(matrix["kind"], "record")
        self.assertEqual(matrix["methods"], [])
        self.assertEqual(matrix["fields"][0]["type"]["count"], 16)
        self.assertEqual(matrix["fields"][0]["default"].split().count("1.f"), 4)
        self.assertEqual(reference["methods"], [])
        self.assertEqual([field["name"] for field in reference["fields"]], ["URI", "Version"])
        self.assertEqual([field["default"] for field in reference["fields"]], ["nullptr", "0"])



if __name__ == "__main__":
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--target", default=API_TARGET)
    parser.add_argument("--clang-arg", action="append", default=[])
    options, remaining = parser.parse_known_args()
    API_TARGET = options.target
    CLANG_ARGS = options.clang_arg
    unittest.main(argv=[sys.argv[0]] + remaining)
