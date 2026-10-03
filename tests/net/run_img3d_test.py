#!/usr/bin/env python3
"""Runs build/host/test_img3d against a fake image-to-3D service (Meshy's
API shape): a job on a data-URI picture, two looks before it is done, the
.glb of tests/ai/glbfix.py to download, a refused key, a failed job."""
import http.server
import json
import os
import socketserver
import subprocess
import sys
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "ai"))
import glbfix  # noqa: E402

GLB = glbfix.build("png")
PIC = glbfix.png(4, 4, [200] * 48)
polls = {"job-1": 0}
seen = {}


class H(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def send(self, code, body, ctype="application/json"):
        if isinstance(body, dict):
            body = json.dumps(body).encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def auth(self):
        return self.headers.get("Authorization", "") == "Bearer good-key"

    def do_POST(self):
        n = int(self.headers["Content-Length"])
        body = json.loads(self.rfile.read(n))
        if not self.auth():
            return self.send(401, {"message": "Unauthorized"})
        if self.path != "/image-to-3d":
            return self.send(404, {"message": "no such path"})
        seen.setdefault("body", body)          # the first job: the picture as data
        url = body.get("image_url", "")
        if url.startswith("data:image/png;base64,"):
            self.send(202, {"result": "job-1"})
        elif url.startswith("https://"):
            self.send(202, {"result": "job-url"})
        else:
            self.send(400, {"message": "bad image_url"})

    def do_GET(self):
        if self.path == "/model.glb":
            return self.send(200, GLB, "model/gltf-binary")
        if not self.auth():
            return self.send(401, {"message": "Unauthorized"})
        if self.path == "/image-to-3d/job-1":
            polls["job-1"] += 1
            k = polls["job-1"]
            if k <= 2:
                self.send(200, {"id": "job-1", "status": "IN_PROGRESS" if k == 2 else "PENDING", "progress": 40 * (k - 1)})
            else:
                base = "http://127.0.0.1:%d" % self.server.server_address[1]
                self.send(200, {"id": "job-1", "status": "SUCCEEDED", "progress": 100,
                                "model_urls": {"glb": base + "/model.glb"}, "task_error": None})
        elif self.path == "/image-to-3d/job-fail":
            self.send(200, {"id": "job-fail", "status": "FAILED", "progress": 10,
                            "task_error": {"message": "no face found in the picture"}})
        else:
            self.send(404, {"message": "no such job"})


class S(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True


def main():
    tool = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else "build/img3d"
    os.makedirs(out, exist_ok=True)
    pic = os.path.join(out, "pic.png")
    open(pic, "wb").write(PIC)
    srv = S(("127.0.0.1", 0), H)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    r = subprocess.run([tool, "http://127.0.0.1:%d" % srv.server_address[1], pic])
    srv.shutdown()
    body = seen.get("body", {})
    assert body.get("target_polycount") == 1500 and body.get("topology") == "triangle", body
    assert body.get("should_texture") is True and body.get("ai_model") == "meshy-5", body
    print("img3d: the request's fields ok")
    sys.exit(r.returncode)


if __name__ == "__main__":
    main()
