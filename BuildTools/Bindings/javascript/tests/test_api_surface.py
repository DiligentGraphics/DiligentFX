"""Check API surfaces using real JavaScript fixture declarations."""

import json
from pathlib import Path
import sys
import unittest

TOOLS = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(TOOLS / "tests"))
import api_surface_cases
from javascript.generate import Generator
from configuration import load_config

MODEL_PATH = Path(sys.argv.pop(1))


class APISurfaceTest(api_surface_cases.APISurfaceCases):
    model_data = json.loads(MODEL_PATH.read_text(encoding="utf-8"))
    adapters = load_config(TOOLS / "tests/native/Adapters.json")
    target = "wasm32"

    def make_generator(self, model, adapters):
        return Generator(model, "BindingTests", ["Interfaces.h"], adapters)

    def test_requires_wasm32(self):
        model = dict(self.model_data, target="x86_64-unknown-linux-gnu")
        from binding_model import ModelError
        with self.assertRaisesRegex(ModelError, "Wasm32"):
            self.surface(model)


if __name__ == "__main__":
    unittest.main()
