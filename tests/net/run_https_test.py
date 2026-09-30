#!/usr/bin/env python3
"""Runs tests/net/test_https.c against local TLS servers with a test CA."""
import datetime
import http.server
import os
import socketserver
import ssl
import subprocess
import sys
import tempfile
import threading


NOW = datetime.datetime.now(datetime.timezone.utc)
DAY = datetime.timedelta(days=1)


def sh(*args, cwd):
    subprocess.run(args, cwd=cwd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


CA_CONF = """[ ca ]
default_ca = ca
[ ca ]
dir = .
database = %(tag)s.db
new_certs_dir = .
serial = %(tag)s.srl
default_md = sha256
policy = any
copy_extensions = copy
unique_subject = no
[ any ]
commonName = supplied
[ leaf ]
subjectAltName = DNS:localhost
basicConstraints = CA:false
"""


def make_ca(tmp, tag, cn):
    sh("openssl", "req", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:P-256", "-nodes",
       "-keyout", tag + ".key", "-out", tag + ".crt", "-days", "30", "-subj", "/CN=" + cn,
       "-addext", "basicConstraints=critical,CA:true", cwd=tmp)
    open(os.path.join(tmp, tag + ".db"), "w").close()
    open(os.path.join(tmp, tag + ".srl"), "w").write("01\n")
    open(os.path.join(tmp, tag + ".cnf"), "w").write(CA_CONF % {"tag": tag})


def stamp(dt):
    return dt.strftime("%Y%m%d%H%M%SZ")


def make_leaf(tmp, ca, tag, keytype, before, after):
    if keytype == "ec":
        sh("openssl", "req", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:P-256", "-nodes",
           "-keyout", tag + ".key", "-out", tag + ".csr", "-subj", "/CN=localhost", cwd=tmp)
    else:
        sh("openssl", "req", "-newkey", "rsa:2048", "-nodes",
           "-keyout", tag + ".key", "-out", tag + ".csr", "-subj", "/CN=localhost", cwd=tmp)
    sh("openssl", "ca", "-batch", "-config", ca + ".cnf", "-cert", ca + ".crt", "-keyfile", ca + ".key",
       "-in", tag + ".csr", "-out", tag + ".crt", "-extensions", "leaf",
       "-startdate", stamp(before), "-enddate", stamp(after), "-notext", cwd=tmp)


BIG = bytes((i * 7) & 0xFF for i in range(1 << 20))


class H(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def send(self, code, body, extra=()):
        self.send_response(code)
        self.send_header("Content-Length", str(len(body)))
        for k, v in extra:
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/hello":
            self.send(200, b"hello, bm\n")
        elif self.path == "/big":
            self.send(200, BIG)
        elif self.path == "/chunked":
            self.send_response(200)
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            for ch in (b"a" * 1000, b"b" * 1000, b"c" * 1000):
                self.wfile.write(b"%x\r\n%s\r\n" % (len(ch), ch))
            self.wfile.write(b"0\r\n\r\n")
        elif self.path == "/redirect-plain":
            self.send(302, b"", [("Location", "https://localhost:%d/hello" % PORTS["rsa"])])
        else:
            self.send(404, b"")


class S(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True


PORTS = {}


def serve(tag, tmp):
    kp, cp = os.path.join(tmp, tag + ".key"), os.path.join(tmp, tag + ".crt")
    srv = S(("127.0.0.1", 0), H)
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(cp, kp)
    srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
    PORTS[tag] = srv.server_address[1]
    threading.Thread(target=srv.serve_forever, daemon=True).start()


with tempfile.TemporaryDirectory() as tmp:
    make_ca(tmp, "ca", "bm test CA")
    make_ca(tmp, "other_ca", "some other CA")
    make_leaf(tmp, "ca", "ec", "ec", NOW - DAY, NOW + 10 * DAY)
    make_leaf(tmp, "ca", "rsa", "rsa", NOW - DAY, NOW + 10 * DAY)
    make_leaf(tmp, "ca", "old", "ec", NOW - 20 * DAY, NOW - 10 * DAY)
    make_leaf(tmp, "other_ca", "other", "ec", NOW - DAY, NOW + 10 * DAY)
    for t in ("ec", "rsa", "old", "other"):
        serve(t, tmp)
    r = subprocess.run([sys.argv[1], os.path.join(tmp, "ca.crt")]
                       + [str(PORTS[t]) for t in ("ec", "rsa", "old", "other")])
    sys.exit(r.returncode)
