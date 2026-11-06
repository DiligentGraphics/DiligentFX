"""Generation diagnostics, reproducibility, and C compatibility of the fixtures."""

import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

import support
from support import build_fixture, struct_config
from binding_model import ModelError
from clang_parser import configure_clang
from clang import cindex


class GenerationTest(unittest.TestCase):
    def test_deterministic_and_independent_of_checkout_path(self):
        config = struct_config()
        config["declarations"]["Diligent::BindingTestVector"] = {}
        model = build_fixture(config)
        config["declarations"] = dict(reversed(list(config["declarations"].items())))
        self.assertEqual(model, build_fixture(config))
        self.assertEqual(model["target"], support.TARGET)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            destination = root / "tests/fixtures"
            destination.mkdir(parents=True)
            shutil.copyfile(support.TOOLS / "tests/fixtures/Structs.h", destination / "Structs.h")
            self.assertEqual(model, build_fixture(config, root))

    def test_missing_declaration(self):
        config = struct_config()
        config["declarations"]["Diligent::Missing"] = {}
        with self.assertRaisesRegex(ModelError, "Declarations not found: Diligent::Missing"):
            build_fixture(config)

    def test_clang_error_is_reported(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "Invalid.h").write_text('''
#include "BasicTypes.h"
DILIGENT_BEGIN_NAMESPACE(Diligent)
struct Invalid { UnknownType Value; };
typedef struct Invalid Invalid;
DILIGENT_END_NAMESPACE
''', encoding="utf-8")
            config = {"schema_version": 1, "headers": ["Invalid.h"], "declarations": {"Diligent::Invalid": {}}}
            with self.assertRaisesRegex(ModelError, "Clang rejected the API headers"):
                build_fixture(config, root)

    def test_cli_generates_artifact_without_rewriting_unchanged_output(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / "config.json"
            output = Path(directory) / "generated/api.json"
            config.write_text(json.dumps(struct_config()), encoding="utf-8")
            command = [sys.executable, "-B", str(support.TOOLS / "generate.py"),
                       "--source-root", str(support.TOOLS), "--config", str(config),
                       "--target", support.TARGET, "--output", str(output)]
            command += [f"--clang-arg={arg}" for arg in support.clang_arguments()]
            subprocess.run(command, check=True, capture_output=True, text=True)
            stamp = output.stat().st_mtime_ns
            subprocess.run(command, check=True, capture_output=True, text=True)
            self.assertEqual(output.stat().st_mtime_ns, stamp)
            self.assertEqual(json.loads(output.read_text(encoding="utf-8")), build_fixture(struct_config()))

    def test_fixtures_are_c_compatible(self):
        configure_clang()
        headers = sorted((support.TOOLS / "tests/fixtures").glob("*.h"))
        # Exercise the C call macros too, so a fixture cannot silently become C++-only.
        source = "".join(f'#include "{header.as_posix()}"\n' for header in headers) + '''
void UseTestAPI(IBindingTestObject* pObject, IBindingTestMutableObject* pMutable)
{
    const void* pData = 0;
    IBindingTestObject_GetSize(pObject);
    IBindingTestObject_Read(pObject, &pData);
    IBindingTestMutableObject_Resize(pMutable, 16, True, False);
    BindingTestDesc Desc = {0};
    Diligent_CreateBindingTestObject(&Desc, &pObject);
}
'''
        unit = cindex.Index.create().parse(
            "fixtures.c", args=["-x", "c", "-std=c11", f"--target={support.TARGET}"] + support.clang_arguments(),
            unsaved_files=[("fixtures.c", source)])
        errors = [str(diagnostic) for diagnostic in unit.diagnostics if diagnostic.severity >= cindex.Diagnostic.Error]
        self.assertEqual(errors, [])
