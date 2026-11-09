"""Run the complete Radient render manifest in one browser/Wasm session."""

import argparse
import json
import os
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[6]
sys.path.insert(0, str(ROOT / "BuildTools/Bindings/javascript/tests"))
from run_browser_tests import run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--module", type=Path, required=True)
    parser.add_argument("--models", type=Path, default=os.environ.get("RADIENT_BROWSER_TEST_MODELS"))
    parser.add_argument("--assets", type=Path, default=os.environ.get("RADIENT_BROWSER_TEST_ASSETS"))
    parser.add_argument("--reference-directory", type=Path, default=os.environ.get("RADIENT_BROWSER_TEST_REFERENCES"),
                        help="Reference image directory (defaults to <assets>/GoldenImages/Chrome/GLTF)")
    parser.add_argument("--output", type=Path, default=os.environ.get("RADIENT_BROWSER_TEST_OUTPUT", "BrowserRenderResults"))
    parser.add_argument("--manifest", type=Path, default=ROOT / "Tests/RadientRenderTest/assets/RadientRenderTests.json")
    parser.add_argument("--filter", default="*", help="Optional case-name glob; all manifest entries run by default")
    parser.add_argument("--browser", default=os.environ.get("BINDINGS_TEST_BROWSER_EXECUTABLE"))
    parser.add_argument("--width", type=int, default=512)
    parser.add_argument("--height", type=int, default=512)
    parser.add_argument("--timeout", type=float, default=7100)
    parser.add_argument("--case-timeout", type=float, default=180)
    parser.add_argument("--max-channel-error", type=int, default=2)
    parser.add_argument("--max-bad-pixel-ratio", type=float, default=0.0001)
    parser.add_argument("--update-golden-images", action="store_true")
    args = parser.parse_args()
    if not args.models or not args.assets:
        parser.error("Set --models/--assets or RADIENT_BROWSER_TEST_MODELS/RADIENT_BROWSER_TEST_ASSETS")
    if min(args.width, args.height, args.timeout, args.case_timeout) <= 0:
        parser.error("Dimensions and timeouts must be positive")
    if not 0 <= args.max_channel_error <= 255 or not 0 <= args.max_bad_pixel_ratio <= 1:
        parser.error("Invalid comparison tolerance")
    args.models = args.models.resolve(strict=True)
    args.assets = args.assets.resolve(strict=True)
    references = (args.reference_directory or args.assets / "GoldenImages/Chrome/GLTF").resolve()
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    args.suite = Path(__file__).with_name("run.mjs")
    args.shared_memory = True
    config = {"manifest": json.loads(args.manifest.read_text(encoding="utf-8")),
              "filter": args.filter, "width": args.width, "height": args.height,
              "caseTimeout": args.case_timeout,
              "maxChannelError": args.max_channel_error, "maxBadPixelRatio": args.max_bad_pixel_ratio}

    def handle_post(handler):
        if handler.path not in ("/progress", "/artifact", "/render-report"):
            return False
        size = int(handler.headers.get("Content-Length", "0"))
        if not 0 < size <= 32 * 1024 * 1024:
            handler.send_error(400, "Invalid report size")
            return True
        data = handler.rfile.read(size)
        if handler.path == "/artifact":
            name = handler.headers.get("X-Artifact-Name", "")
            if not re.fullmatch(r"[A-Za-z0-9_]+/[A-Za-z0-9_]+\.png", name):
                handler.send_error(400, "Invalid artifact name")
                return True
            output = args.output / name
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_bytes(data)
            if args.update_golden_images and not name.endswith("_diff.png"):
                destination = references / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(data)
        elif handler.path == "/progress":
            print(json.loads(data)["message"], flush=True)
        else:
            report = json.loads(data)
            (args.output / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        handler.send_response(204)
        handler.end_headers()
        return True

    return run(args, extra_roots={"models": args.models, "assets": args.assets, "references": references},
               test_config=config, entry_point="/tests/run.mjs", handle_post=handle_post)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f"Browser render tests failed: {error}", file=sys.stderr)
        sys.exit(1)
