"""Extend the production binding model with image helpers for the test executable."""

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[6]
sys.path.insert(0, str(ROOT / "BuildTools/Bindings"))
from generate import build_model

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--target", required=True)
parser.add_argument("--clang-arg", action="append", default=[])
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
config = json.loads((ROOT / "Radient/bindings/API.json").read_text(encoding="utf-8"))
header = "Radient/bindings/javascript/tests/browser/render/NativeCapture.h"
config["headers"].append(header)
config["exports"]["headers"].append(header)
model = build_model(ROOT, config, args.target, args.clang_arg)
text = json.dumps(model, indent=2, ensure_ascii=False) + "\n"
args.output.parent.mkdir(parents=True, exist_ok=True)
if not args.output.exists() or args.output.read_text(encoding="utf-8") != text:
    args.output.write_text(text, encoding="utf-8", newline="\n")
