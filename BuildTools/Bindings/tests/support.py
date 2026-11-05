"""Shared fixture paths, target configuration, and binding policies."""

from pathlib import Path
import sys

TOOLS = Path(__file__).resolve().parents[1]
CORE_HEADERS = TOOLS.parents[2] / "DiligentCore/Primitives/interface"
sys.path.insert(0, str(TOOLS))

from clang_parser import extract_api
from generate import build_model


TARGET = "x86_64-pc-windows-msvc" if sys.platform == "win32" else "x86_64-unknown-linux-gnu"
CLANG_ARGS = []
BORROWED = {"direction": "in", "nullable": False, "ownership": "borrowed", "lifetime": "call"}
OUTPUT = {"direction": "out", "nullable": False, "ownership": "owned", "lifetime": "caller"}


def clang_arguments():
    return [f"-I{CORE_HEADERS}"] + CLANG_ARGS


def declarations(model):
    return {declaration["name"]: declaration for declaration in model["declarations"]}


def extract_fixture(header, names):
    model, _ = extract_api(TOOLS, [f"tests/fixtures/{header}"], names, TARGET, clang_arguments())
    return declarations(model)


def struct_config():
    return {
        "schema_version": 1,
        "headers": ["tests/fixtures/Structs.h"],
        "declarations": {
            "Diligent::BindingTestDesc": {"fields": {"pData": dict(BORROWED, length="Size")}},
        },
    }


def function_config():
    config = struct_config()
    config["headers"] = ["tests/fixtures/Functions.h"]
    config["declarations"].update({
        "Diligent::BINDING_TEST_STATUS": {"role": "status"},
        "Diligent::IObject": {"binding": "external", "reason": "Runtime reference counting"},
        "Diligent::IBindingTestObject": {
            "iid": "Diligent::IID_BindingTestObject",
            "methods": {
                "Read": {
                    "parameters": {"ppData": dict(OUTPUT, ownership="borrowed", lifetime="read_scope")}
                }
            },
        },
        "Diligent::CreateBindingTestObject": {
            "parameters": {"Desc": BORROWED.copy(), "ppObject": OUTPUT.copy()}
        },
    })
    return config


def build_fixture(config, root=TOOLS):
    return build_model(root, config, TARGET, clang_arguments())
