#!/usr/bin/env python3
"""Runs build/host/test_webget (web_get, image decoding and the rules of the web API) against a local server.

  run_webget_test.py BINARY

The server has the pages the test asks for: a short page, a 404, a chunked body, a slow body (pieces
150 ms apart), a 3 MB body and redirects (a chain of 3, a chain of 4, to https, relative).
"""
import http.server
import os
import socketserver
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
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
        port = self.server.server_address[1]
        chain = {"/r0": "/r1", "/r1": "/r2", "/r2": "/r3", "/r3": "/hello"}
        try:
            if p == "/hello":
                self.send(200, b"hello, bm\n")
            elif p in chain:
                self.send(302, b"", extra=[("Location", chain[p])])
            elif p == "/redirect-rel":
                self.send(301, b"", extra=[("Location", "/hello")])
            elif p == "/to-https":
                self.send(302, b"", extra=[("Location", "https://127.0.0.1:%d/hello" % port)])
            elif p == "/chunked":
                self.send_response(200)
                self.send_header("Transfer-Encoding", "chunked")
                self.end_headers()
                for ch in (b"a" * 1000, b"b" * 1000, b"c" * 1000):
                    self.wfile.write(b"%x\r\n%s\r\n" % (len(ch), ch))
                self.wfile.write(b"0\r\n\r\n")
            elif p == "/slow":
                self.send_response(200)
                self.send_header("Content-Type", "text/plain")
                self.send_header("Content-Length", "3000")
                self.end_headers()
                for _ in range(3):
                    self.wfile.write(b"s" * 1000)
                    self.wfile.flush()
                    time.sleep(0.15)
            elif p == "/big":
                self.send(200, BIG, "application/octet-stream")
            else:
                self.send(404, b"not here")
        except (BrokenPipeError, ConnectionResetError):
            pass


class S(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True


srv = S(("127.0.0.1", 0), H)
threading.Thread(target=srv.serve_forever, daemon=True).start()
r = subprocess.run([sys.argv[1], str(srv.server_address[1]), os.path.join(HERE, "fixtures")])
srv.shutdown()
sys.exit(r.returncode)
