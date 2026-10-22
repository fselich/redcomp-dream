"""Run the public account API in a fresh process with a private HTTP fixture."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import subprocess
import tempfile
import threading


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, required=True)
    args = parser.parse_args()
    requests = []

    class Handler(BaseHTTPRequestHandler):
        def do_POST(self):
            request = self.rfile.read(int(self.headers.get("Content-Length", "0")))
            requests.append((self.path, request))
            body = json.dumps({"url": "https://example.invalid/fixture", "code": "fixture"}).encode()
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *args):
            pass

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        with tempfile.TemporaryDirectory(prefix="rnet-first-auth-") as directory:
            result = subprocess.run(
                [str(args.probe.resolve()), f"ws://127.0.0.1:{server.server_port}"],
                cwd=directory, capture_output=True, text=True, timeout=15,
            )
            assert result.returncode == 0, result.stdout + result.stderr
            assert requests == [("/auth/discord/start", b"{}")], requests
            assert list(Path(directory).iterdir()) == [], "Fixture created account data"
    finally:
        server.shutdown()
        server.server_close()
        worker.join(timeout=2)
    print("First account HTTP request passed without prior socket startup or browser")


if __name__ == "__main__":
    main()
