"""Run binding unit tests against the configured native target and Diligent headers."""

import argparse
from pathlib import Path
import unittest

import support


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", default=support.TARGET)
    parser.add_argument("--clang-arg", action="append", default=[])
    parser.add_argument("--pattern", default="test_*.py", help="Select a test module, e.g. test_structs.py")
    options = parser.parse_args()
    support.TARGET = options.target
    support.CLANG_ARGS = options.clang_arg
    suite = unittest.defaultTestLoader.discover(str(Path(__file__).parent), pattern=options.pattern)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    raise SystemExit(0 if result.wasSuccessful() else 1)
