#!/usr/bin/env python3
"""Serve the local scenario workbench. Standard library only; no cloud calls."""

import argparse
import hashlib
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

from model import canonical, integer
from run import compare, execute
from scenarios import presets, workload


ROOT = Path(__file__).resolve().parent
STATIC = {"/": ("index.html", "text/html"), "/app.js": ("app.js", "text/javascript"),
          "/style.css": ("style.css", "text/css")}


class Handler(BaseHTTPRequestHandler):
    def reply(self, status, data, content_type="application/json"):
        body = json.dumps(data, separators=(",", ":")).encode() if content_type == "application/json" else data
        self.send_response(status)
        self.send_header("Content-Type", content_type + "; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/api/presets":
            self.reply(200, presets())
        elif self.path in STATIC:
            file, content_type = STATIC[self.path]
            self.reply(200, (ROOT / file).read_bytes(), content_type)
        else:
            self.reply(404, {"error": "Not found"})

    def do_POST(self):
        # Keep browser requests local to the page that supplied the model editor.
        origin = self.headers.get("Origin")
        if origin and urlparse(origin).netloc != self.headers.get("Host"):
            self.reply(403, {"error": "Cross-origin requests are not supported"})
            return
        try:
            size = integer(int(self.headers.get("Content-Length", "0")), "request bytes", 1, 512000)
            data = json.loads(self.rfile.read(size))
            if self.path == "/api/generate":
                result = workload(**data)
            elif self.path in ("/api/run", "/api/compare", "/api/save"):
                steps = integer(data.get("max_steps", 2000), "max_steps", 1, 2000)
                if self.path == "/api/run":
                    result = execute(data["scenario"], steps, frames=True)
                elif self.path == "/api/compare":
                    result = compare(data["scenario"], steps)
                else:
                    result = execute(data["scenario"], steps)
                    if (result["trace_sha256"] != data["trace_sha256"]
                            or result["model_files_sha256"] != data["model_files_sha256"]):
                        raise ValueError("The displayed run no longer matches this engine. Run again before saving.")
                    encoded = (canonical(result) + "\n").encode()
                    name = hashlib.sha256(encoded).hexdigest() + ".json"
                    relative = Path("build/orbital-scenarios/replays") / name
                    output = ROOT.parents[2] / relative
                    output.parent.mkdir(parents=True, exist_ok=True)
                    if output.exists():
                        if output.read_bytes() != encoded:
                            raise ValueError("Replay path exists with different bytes")
                    else:
                        output.write_bytes(encoded)
                    result = {"path": str(output), "relative_path": str(relative), "trace_sha256": result["trace_sha256"]}
            else:
                self.reply(404, {"error": "Not found"})
                return
            self.reply(200, result)
        except (ValueError, TypeError, KeyError, AttributeError) as error:
            self.reply(400, {"error": str(error)})
        except AssertionError as error:
            self.reply(422, {"error": f"Model invariant failed: {error}. Save this scenario as a counterexample."})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8767)
    args = parser.parse_args()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    print(f"Orbital scenario workbench: http://127.0.0.1:{server.server_port}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
