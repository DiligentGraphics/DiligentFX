"""Run RadientGPUTest in headless Chrome with WebGPU."""

from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[3] / "DiligentCore" / "Tests" / "TestFramework" / "browser"))
from browser_tests import main


if __name__ == "__main__":
    sys.exit(main(default_mode="wgpu", default_exclude_file=HERE / "exclude.txt"))
