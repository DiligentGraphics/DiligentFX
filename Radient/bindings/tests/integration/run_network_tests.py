"""Run a binding suite against the shared loopback HTTP fixtures.

The command after -- is executed unchanged. --browser-fixtures adds a temporary
--test-data file containing network.baseURL for the generic browser runner.
Node and Java obtain the same URL from RADIENT_TEST_NETWORK_URL.
"""

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[3] / "Tests" / "RadientCommon" / "python"))
from network_server import handle_request, PREFIX


class Handler(BaseHTTPRequestHandler):
    def end_headers(self):
        # Browser bindings are hosted by their own runner, on another local port.
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def serve(self):
        if not handle_request(self):
            self.send_error(404)

    do_GET = serve
    do_HEAD = serve

    def log_message(self, *_):
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--browser-fixtures", type=Path)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        parser.error("A test command is required after --")
    with ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            base = f"http://127.0.0.1:{server.server_port}{PREFIX}"
            env = {**os.environ, "RADIENT_TEST_NETWORK_URL": base}
            with tempfile.TemporaryDirectory(prefix="radient-network-") as temporary:
                if args.browser_fixtures:
                    fixtures = Path(temporary) / "fixtures.properties"
                    fixtures.write_text(args.browser_fixtures.read_text(encoding="utf-8") +
                                        f"\nnetwork.baseURL={base}\n", encoding="utf-8")
                    command += ["--test-data", str(fixtures)]
                return subprocess.run(command, env=env, timeout=75, check=False).returncode
        finally:
            server.shutdown()
            thread.join()


if __name__ == "__main__":
    sys.exit(main())
