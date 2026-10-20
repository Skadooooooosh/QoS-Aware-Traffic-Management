#!/usr/bin/env python3
"""Compiles the C++ engine and serves the dashboard + /api/simulate. Standard library only."""
import json, os, subprocess, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

BASE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(BASE, "qos_engine.cpp")
EXE = os.path.join(BASE, "qos_engine.exe" if os.name == "nt" else "qos_engine")
SEED = 42
# name: (default, min, max)
LIMITS = {"link": (100, 10, 1000), "dur": (1, 0.5, 3), "voice": (6, 0, 64), "video": (3, 0, 12), "dl": (8, 1, 8),
          "gap": (50, 20, 2000), "vq": (3000, 500, 9000), "bq": (1500, 500, 9000), "prop": (5, 0, 100)}

def compile_engine():
    if os.path.exists(EXE) and os.path.getmtime(EXE) >= os.path.getmtime(SRC):
        return
    print("Compiling C++ engine...")
    subprocess.run(["g++", "-O2", "-std=c++17", "-o", EXE, SRC], check=True)

def run_engine(qs):
    v = {}
    for k, (d, lo, hi) in LIMITS.items():
        try: x = float(qs.get(k, [d])[0])
        except ValueError: x = d
        v[k] = max(lo, min(hi, x))
    args = [EXE] + [str(v[k]) for k in ("link", "dur", "voice", "video", "dl", "gap", "vq", "bq")] + [str(SEED), str(v["prop"])]
    t0 = time.perf_counter()
    out = subprocess.run(args, capture_output=True, text=True, timeout=60)
    if out.returncode != 0:
        raise RuntimeError(out.stderr or "engine failed")
    data = json.loads(out.stdout)
    data["elapsed"] = round(time.perf_counter() - t0, 2)
    data["seed"] = SEED
    return json.dumps(data)

class Handler(BaseHTTPRequestHandler):
    def _send(self, code, body, ctype="application/json"):
        data = body.encode() if isinstance(body, str) else body
        self.send_response(code); self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data))); self.end_headers(); self.wfile.write(data)

    def do_GET(self):
        url = urlparse(self.path)
        if url.path in ("/", "/index.html"):
            with open(os.path.join(BASE, "index.html"), "rb") as f:
                return self._send(200, f.read(), "text/html; charset=utf-8")
        if url.path == "/api/health":
            return self._send(200, json.dumps({"engine": "active"}))
        if url.path == "/api/simulate":
            try: return self._send(200, run_engine(parse_qs(url.query)))
            except Exception as e: return self._send(500, json.dumps({"error": str(e)}))
        self._send(404, json.dumps({"error": "not found"}))

    def log_message(self, *a): pass

if __name__ == "__main__":
    compile_engine()
    print("QoS dashboard running at http://localhost:8000")
    ThreadingHTTPServer(("0.0.0.0", 8000), Handler).serve_forever()
