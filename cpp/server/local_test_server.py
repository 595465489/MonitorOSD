"""本地联调用的等价服务端（验证 C++ 客户端的 HTTP/HMAC/清单协议）。
与 feedback.php / latest.json 逻辑一一对应，仅用于开发验证，不部署。
"""
import base64
import hashlib
import hmac
import json
import os
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer

SALT = b"change-me-feedback-salt"
ROOT = os.path.dirname(os.path.abspath(__file__))
STORE = os.path.join(ROOT, "_test_feedback")
os.makedirs(STORE, exist_ok=True)


def make_manifest(path):
    from pathlib import Path
    pkg = Path(path)
    data = pkg.read_bytes()
    return {
        "version": sys.argv[2] if len(sys.argv) > 2 else "1.9.9",
        "url": f"http://127.0.0.1:{PORT}/pkgs/{pkg.name}",
        "sha256": hashlib.sha256(data).hexdigest(),
        "size": len(data),
        "force": False,
        "notes": ["联调用假包：仅验证下载与校验链路", "第二条更新说明"],
    }


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def do_GET(self):
        if self.path.endswith(".json"):
            pkg = os.path.join(ROOT, "_dummy_pkg.exe")
            if not os.path.exists(pkg):
                with open(pkg, "wb") as f:
                    f.write(b"MZ" + b"\0" * 4096)
            body = json.dumps(make_manifest(pkg), ensure_ascii=False).encode()
            self._send(200, body, "application/json")
        elif self.path.startswith("/pkgs/"):
            pkg = os.path.join(ROOT, "_dummy_pkg.exe")
            self._send(200, open(pkg, "rb").read(), "application/octet-stream")
        else:
            self._send(404, b"not found")

    def do_POST(self):
        raw = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        try:
            data = json.loads(raw)
        except Exception as e:
            with open(os.path.join(STORE, "rejected.log"), "ab") as f:
                f.write(b"BAD JSON: " + str(e).encode() + b" | " + raw[:300] + b"\n")
            return self._send(400, b'{"ok":false,"error":"bad json"}')
        h = data.get("hash", "")
        pos = raw.rfind(b',"hash"')
        if pos < 0:
            with open(os.path.join(STORE, "rejected.log"), "ab") as f:
                f.write(b"BAD LAYOUT: " + raw[:300] + b"\n")
            return self._send(400, b'{"ok":false,"error":"bad layout"}')
        payload = raw[:pos] + b"}"
        expect = base64.b64encode(hmac.new(SALT, payload, hashlib.sha256).digest()).decode()
        if not hmac.compare_digest(expect, h):
            with open(os.path.join(STORE, "rejected.log"), "ab") as f:
                f.write(b"BAD SIGN: " + raw[:200] + b"\n")
            return self._send(403, b'{"ok":false,"error":"bad sign"}')
        with open(os.path.join(STORE, "ok.jsonl"), "ab") as f:
            f.write(json.dumps(data, ensure_ascii=False).encode() + b"\n")
        self._send(200, b'{"ok":true}')

    def _send(self, code, body, ctype="application/json"):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 18080
print(f"serving on 127.0.0.1:{PORT}", flush=True)
HTTPServer(("127.0.0.1", PORT), H).serve_forever()
