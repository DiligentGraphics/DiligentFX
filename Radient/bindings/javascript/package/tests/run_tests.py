"""Install the exact npm tarball into an independent application, build it, and run Chrome."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

HERE = Path(__file__).resolve().parent
FX = HERE.parents[4]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tarball", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True, help="New independent consumer directory")
    parser.add_argument("--browser")
    args = parser.parse_args()
    tarball = args.tarball.resolve(strict=True)
    work = args.work_dir.resolve()
    shutil.copytree(HERE / "consumer", work, ignore=shutil.ignore_patterns("node_modules", "dist", "public", ".webpack-src"))
    npm = shutil.which("npm.cmd") or shutil.which("npm")
    if not npm:
        raise RuntimeError("npm is required")
    for command in ([npm, "ci", "--ignore-scripts", "--no-audit", "--no-fund"],
                    [npm, "install", "--no-save", "--package-lock=false", "--ignore-scripts", "--no-audit", "--no-fund", str(tarball)],
                    [npm, "run", "build"]):
        subprocess.run(command, cwd=work, check=True)
    subprocess.run(["node", str(work / "node.mjs")], cwd=work, check=True)
    for bundler in ("vite", "webpack"):
        print(f"Testing installed package in Chrome ({bundler})", flush=True)
        command = [sys.executable, "-B", str(FX / "Radient/bindings/tests/integration/run_network_tests.py"),
                   "--", sys.executable, "-B", str(HERE / "run_browser.py"), "--dist", str(work / "dist" / bundler)]
        if args.browser:
            command += ["--browser", args.browser]
        subprocess.run(command, check=True)


if __name__ == "__main__":
    main()
