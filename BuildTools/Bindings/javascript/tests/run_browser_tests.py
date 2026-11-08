"""Run a shared JavaScript test suite in headless Chrome, Chromium, or Edge.

Only the generated module, suite, and browser harness directories are served.
The browser posts structured results; missing results, startup errors, and test
failures all fail the command. No browser automation package is required.
"""

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import mimetypes
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from urllib.parse import quote, unquote, urlsplit


def find_browser(explicit):
    if explicit:
        return explicit
    for name in ("google-chrome", "google-chrome-stable", "chromium", "chromium-browser", "microsoft-edge"):
        executable = shutil.which(name)
        if executable:
            return executable
    candidates = [Path("/Applications/Google Chrome.app/Contents/MacOS/Google Chrome")]
    for variable in ("PROGRAMFILES", "PROGRAMFILES(X86)", "LOCALAPPDATA"):
        if os.environ.get(variable):
            root = Path(os.environ[variable])
            candidates.extend((root / "Google/Chrome/Application/chrome.exe",
                               root / "Microsoft/Edge/Application/msedge.exe"))
    for candidate in candidates:
        if candidate.is_file():
            return str(candidate)
    raise RuntimeError("Chrome/Chromium/Edge was not found; pass --browser with its executable path")


def run(args):
    module = args.module.resolve(strict=True)
    suite = args.suite.resolve(strict=True)
    roots = {"generated": module.parent, "tests": suite.parent,
             "harness": Path(__file__).resolve().parent / "browser"}
    config = json.dumps({"module": "/generated/" + quote(module.name),
                         "suite": "/tests/" + quote(suite.name),
                         "sharedMemory": args.shared_memory}).replace("<", "\\u003c")
    page = ("<!doctype html><meta charset=utf-8><title>Binding tests</title>"
            '<script id="config" type="application/json">' + config + '</script>'
            '<pre id="result">Running binding tests...</pre>'
            '<script type="module" src="/harness/run_tests.mjs"></script>').encode()
    completed = threading.Event()
    reports = []

    class Handler(BaseHTTPRequestHandler):
        def end_headers(self):
            # The production Radient module uses Emscripten pthreads. Serve all
            # resources from one isolated origin, including Wasm and worker files.
            self.send_header("Cross-Origin-Opener-Policy", "same-origin")
            self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
            self.send_header("Cache-Control", "no-store")
            super().end_headers()

        def do_GET(self):
            path = unquote(urlsplit(self.path).path)
            if path == "/":
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.end_headers()
                self.wfile.write(page)
                return
            parts = path.lstrip("/").split("/", 1)
            root = roots.get(parts[0])
            file = (root / parts[1]).resolve() if root and len(parts) == 2 else None
            if file is None or not file.is_relative_to(root) or not file.is_file():
                self.send_error(404)
                return
            content_type = {".mjs": "text/javascript", ".wasm": "application/wasm"}.get(file.suffix)
            self.send_response(200)
            self.send_header("Content-Type", content_type or mimetypes.guess_type(file.name)[0] or "application/octet-stream")
            self.send_header("Content-Length", str(file.stat().st_size))
            self.end_headers()
            with file.open("rb") as source:
                shutil.copyfileobj(source, self.wfile)

        def do_POST(self):
            if self.path != "/result":
                self.send_error(404)
                return
            try:
                size = int(self.headers.get("Content-Length", "0"))
                if not 0 < size <= 1024 * 1024:
                    raise ValueError("Invalid browser report size")
                report = json.loads(self.rfile.read(size))
                if not isinstance(report, dict):
                    raise ValueError("Invalid browser report")
            except (ValueError, UnicodeError) as error:
                report = {"fatal": str(error)}
            reports.append(report)
            self.send_response(204)
            self.end_headers()
            completed.set()

        def log_message(self, _format, *arguments):
            pass

    browser = find_browser(args.browser)
    with ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            with tempfile.TemporaryDirectory(prefix="binding-browser-", ignore_cleanup_errors=True) as directory:
                log_path = Path(directory) / "browser.log"
                command = [browser, "--headless=new", "--no-first-run", "--no-default-browser-check",
                           "--disable-background-networking", "--disable-extensions", "--disable-sync",
                           "--user-data-dir=" + str(Path(directory) / "profile"),
                           f"http://127.0.0.1:{server.server_port}/"]
                print(f"Running browser tests: {browser}", flush=True)
                with log_path.open("w", encoding="utf-8") as log:
                    process = subprocess.Popen(command, stdout=log, stderr=log,
                                               creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
                    try:
                        deadline = time.monotonic() + args.timeout
                        while not completed.wait(0.2):
                            if process.poll() is not None:
                                raise RuntimeError(f"Browser exited before reporting results (exit {process.returncode})")
                            if time.monotonic() >= deadline:
                                raise RuntimeError(f"Browser tests timed out after {args.timeout:g} seconds")
                    except RuntimeError:
                        # Keep startup/crash diagnostics in CI logs without
                        # printing routine Chromium diagnostics on successful runs.
                        print(log_path.read_text(encoding="utf-8", errors="replace")[-8000:], file=sys.stderr)
                        raise
                    finally:
                        process.terminate()
                        try:
                            process.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait()
                return print_report(reports[0])
        finally:
            server.shutdown()
            thread.join()


def print_report(report):
    if "fatal" in report:
        raise RuntimeError(report["fatal"])
    print(report.get("userAgent", "Unknown browser"))
    print(f"Cross-origin isolated: {report.get('crossOriginIsolated')}; shared Wasm memory: {report.get('sharedMemory')}")
    tests = report.get("tests")
    if not isinstance(tests, list) or not tests:
        raise RuntimeError("Browser returned no test results")
    failed = 0
    for result in tests:
        errors = result["errors"]
        print(f"{'FAIL' if errors else 'PASS'}: {result['name']}")
        for error in errors:
            print(error)
        failed += bool(errors)
    print(f"{len(tests) - failed} passed; {failed} failed")
    return 1 if failed else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--module", type=Path, required=True)
    parser.add_argument("--suite", type=Path, required=True)
    parser.add_argument("--browser", default=os.environ.get("BINDINGS_TEST_BROWSER_EXECUTABLE"),
                        help="Chrome, Chromium, or Edge executable (defaults to BINDINGS_TEST_BROWSER_EXECUTABLE or auto-detection)")
    parser.add_argument("--timeout", type=float, default=60, help="Maximum browser test duration in seconds")
    parser.add_argument("--shared-memory", action="store_true", help="Require shared Wasm memory and cross-origin isolation")
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    try:
        return run(args)
    except (OSError, RuntimeError, ValueError) as error:
        print(f"Browser tests failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
