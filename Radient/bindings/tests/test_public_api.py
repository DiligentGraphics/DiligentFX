"""Integration tests for Radient's selected public binding API."""

import argparse
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "BuildTools/Bindings"))
from binding_model import unalias
from clang_parser import extract_api
from generate import build_model


API_TARGET = "x86_64-pc-windows-msvc" if sys.platform == "win32" else "x86_64-unknown-linux-gnu"
CLANG_ARGS = []


def declarations(model):
    return {declaration["name"]: declaration for declaration in model["declarations"]}


def extract_public(headers, names):
    model, _ = extract_api(ROOT, ["Radient/interface/" + header for header in headers],
                           ["Diligent::" + name for name in names], API_TARGET, CLANG_ARGS)
    return declarations(model)


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

    def test_public_constants(self):
        types = extract_public(["RadientAnimation.h", "RadientDataBlob.h", "RadientVertexLayout.h"], [
            "RADIENT_VERTEX_AUTO_OFFSET", "InvalidRadientAnimationObject", "RadientNodeTranslationPropertyName",
            "RadientNodeAnimationSchemaID", "IID_RadientDataBlob"])
        self.assertEqual(types["Diligent::RADIENT_VERTEX_AUTO_OFFSET"]["value"], 4294967295)
        self.assertEqual(types["Diligent::InvalidRadientAnimationObject"]["value"], 18446744073709551615)
        self.assertEqual(types["Diligent::RadientNodeTranslationPropertyName"]["value"], "Translation")
        self.assertEqual(types["Diligent::RadientNodeAnimationSchemaID"]["value"], {
            "Data1": 0xe4add320, "Data2": 0xeeca, "Data3": 0x439f,
            "Data4": [0xa6, 0xc3, 0x1d, 0x8a, 0x25, 0xae, 0xfb, 0xc3],
        })
        self.assertEqual(types["Diligent::IID_RadientDataBlob"]["value"], {
            "Data1": 0x8a49b839, "Data2": 0x9f71, "Data3": 0x45af,
            "Data4": [0xb8, 0xc0, 0x05, 0x8e, 0x75, 0x27, 0x4f, 0x7b],
        })

    def test_public_typedefs(self):
        types = extract_public(["RadientAnimation.h", "RadientDataBlob.h"], [
            "RadientEntityID", "RadientAnimationSchemaID", "RadientDataBlobDestroyCallbackType"])
        entity = types["Diligent::RadientEntityID"]
        self.assertEqual(entity["kind"], "alias")
        self.assertEqual(unalias(entity["type"])["bits"], 64)
        schema = types["Diligent::RadientAnimationSchemaID"]
        self.assertEqual(unalias(schema["type"])["name"], "Diligent::INTERFACE_ID")
        callback = types["Diligent::RadientDataBlobDestroyCallbackType"]["type"]
        self.assertEqual(callback["kind"], "pointer")
        self.assertEqual(callback["target"]["kind"], "function")
        self.assertEqual(callback["target"]["parameters"][0]["kind"], "pointer")

    def test_public_default_arguments(self):
        types = extract_public(["RadientSceneWriter.h", "RadientSkinning.h"], [
            "IRadientSceneWriter", "IRadientSkeletonPose"])
        writer = {method["name"]: method for method in types["Diligent::IRadientSceneWriter"]["methods"]}
        self.assertEqual([param["default"] for param in writer["SetParent"]["parameters"]], [None, None, "True"])
        pose = {method["name"]: method for method in types["Diligent::IRadientSkeletonPose"]["methods"]}
        self.assertEqual([param["default"] for param in pose["ComputeSkinningMatrices"]["parameters"]],
                         [None, None, "False", "True"])

    def test_math_functions_exclude_cpp_overloads(self):
        names = ["MakeAxisRotation", "MakeXRotation", "MakeYRotation", "MakeZRotation"]
        functions = extract_public(["RadientMath.h"], names)
        for name in names:
            with self.subTest(function=name):
                function = functions["Diligent::" + name]
                self.assertEqual(function["result"]["type"]["kind"], "void")
                self.assertEqual(len(function["parameters"]), 3 if name == "MakeAxisRotation" else 2)
                rotation = function["parameters"][-1]
                self.assertEqual(rotation["name"], "Rotation")
                self.assertEqual(rotation["type"]["kind"], "reference")



if __name__ == "__main__":
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--target", default=API_TARGET)
    parser.add_argument("--clang-arg", action="append", default=[])
    options, remaining = parser.parse_known_args()
    API_TARGET = options.target
    CLANG_ARGS = options.clang_arg
    unittest.main(argv=[sys.argv[0]] + remaining)
