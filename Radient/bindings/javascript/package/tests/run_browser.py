"""Serve only the installed consumer's built output and use the shared Chrome harness."""
import argparse
import os
from pathlib import Path
import sys
from types import SimpleNamespace

FX = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(FX / "BuildTools/Bindings/javascript/tests"))
from run_browser_tests import run

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--dist", type=Path, required=True, help="Built consumer directory")
parser.add_argument("--browser", default=os.environ.get("BINDINGS_TEST_BROWSER_EXECUTABLE"))
args = parser.parse_args()
dist = args.dist.resolve()
options = SimpleNamespace(module=dist / "radient/Radient.mjs", suite=dist / "app.js",
                          shared_memory=True, browser=args.browser, timeout=60)
sys.exit(run(options, extra_roots={"viewer": dist}, entry_point="/viewer/app.js",
             test_config={"networkBase": os.environ["RADIENT_TEST_NETWORK_URL"]}))
