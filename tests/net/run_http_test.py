#!/usr/bin/env python3
"""Runs the HTTP client test (build/host/test_http) against a local server."""
import http.server
import socketserver
import subprocess
import sys
import threading

BIG = bytes((i * 31 + (i >> 12)) & 0xFF for i in range(3 << 20))


class H(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def send(self, code, body, ctype="text/plain", extra=()):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        for k, v in extra:
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        p = self.path
        if p == "/hello":
            self.send(200, b"hello, bm\n")
        elif p == "/chunked":
            self.send_response(200)
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            for ch in (b"a" * 1000, b"b" * 1000, b"c" * 1000):
                self.wfile.write(b"%x\r\n%s\r\n" % (len(ch), ch))
            self.wfile.write(b"0\r\n\r\n")
        elif p == "/redirect":
            self.send(302, b"", extra=[("Location", "/hello")])
        elif p == "/redirect-abs":
            self.send(301, b"", extra=[("Location", "http://127.0.0.1:%d/hello" % self.server.server_address[1])])
        elif p == "/big":
            self.send(200, BIG, "application/octet-stream")
        elif p == "/close":
            self.send_response(200)
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(b"z" * 5000)
            self.close_connection = True
        else:
            self.send(404, b"not here")

    def do_PUT(self):
        n = int(self.headers["Content-Length"])
        body = self.rfile.read(n)
        auth = self.headers.get("Authorization", "").split()[-1:]
        self.send(201, b"PUT Bearer " + " ".join(auth).encode() + b" " + body, "application/json")


class S(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True


srv = S(("127.0.0.1", 0), H)
threading.Thread(target=srv.serve_forever, daemon=True).start()
r = subprocess.run([sys.argv[1], str(srv.server_address[1])])
srv.shutdown()
sys.exit(r.returncode)
