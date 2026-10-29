#!/usr/bin/env python3
"""tools/bm_agent_relay.py: a certificate, the token and the API over real TLS, with a fake program
as the backend (no provider is called). Needs the openssl program.

  python3 tests/tools/test_bm_agent_relay.py
"""
import http.client
import json
import os
import ssl
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RELAY = os.path.join(ROOT, "tools", "bm_agent_relay.py")

FAKE = r'''
import sys
text = sys.stdin.read()
if "FAIL" in text:
    sys.stderr.write("quota exhausted\n")
    sys.exit(3)
sys.stdout.write("got %d chars: %s" % (len(text), text[-40:]))
'''


def read(path):
    with open(path) as f:
        return f.read()


class RelayTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.dir = os.path.join(cls.tmp.name, "relay")
        out = subprocess.run([sys.executable, RELAY, "cert", "--ip", "127.0.0.1", "--dir", cls.dir],
                             capture_output=True, text=True, check=True).stdout
        cls.cert_out = out
        cls.token = read(os.path.join(cls.dir, "token")).strip()
        fake = os.path.join(cls.tmp.name, "fake.py")
        with open(fake, "w") as f:
            f.write(FAKE)
        port_file = os.path.join(cls.tmp.name, "port")
        cls.proc = subprocess.Popen([sys.executable, RELAY, "serve", "--backend", "cmd", "--cmd",
                                     "%s %s" % (sys.executable, fake), "--bind", "127.0.0.1", "--port", "0",
                                     "--gap", "0.5", "--dir", cls.dir, "--port-file", port_file],
                                    stdout=subprocess.DEVNULL)
        for _ in range(100):
            if os.path.exists(port_file) and read(port_file):
                break
            time.sleep(0.05)
        cls.port = int(read(port_file))
        cls.ctx = ssl.create_default_context(cafile=os.path.join(cls.dir, "ca.pem"))   # checks the IP in the certificate

    @classmethod
    def tearDownClass(cls):
        cls.proc.terminate()
        cls.proc.wait()
        cls.tmp.cleanup()

    def call(self, method, path, body=None, token=True):
        c = http.client.HTTPSConnection("127.0.0.1", self.port, context=self.ctx, timeout=20)
        h = {"Content-Type": "application/json"}
        if token:
            h["Authorization"] = "Bearer " + (token if isinstance(token, str) else self.token)
        c.request(method, path, body=json.dumps(body) if body is not None else None, headers=h)
        r = c.getresponse()
        data = json.loads(r.read())
        c.close()
        return r.status, data

    def chat(self, msgs, **kw):
        time.sleep(0.6)                  # the relay's pause between two requests (--gap 0.5)
        return self.call("POST", "/v1/chat/completions", dict(model="default", messages=msgs), **kw)

    def test_cert_output(self):
        self.assertIn("ai_url=https://127.0.0.1:8443/v1", self.cert_out)
        self.assertIn("ai_key=" + self.token, self.cert_out)
        self.assertIn("BEGIN CERTIFICATE", self.cert_out)
        self.assertEqual(os.stat(os.path.join(self.dir, "token")).st_mode & 0o077, 0, "the token is private")

    def test_health_and_token(self):
        self.assertEqual(self.call("GET", "/health", token=False)[0], 200)
        st, d = self.call("GET", "/v1/models", token=False)
        self.assertEqual(st, 401)
        st, d = self.chat([{"role": "user", "content": "hi"}], token="bmr-wrong")
        self.assertEqual(st, 401)
        self.assertIn("token", d["error"]["message"])

    def test_reply(self):
        st, d = self.chat([{"role": "system", "content": "be short"}, {"role": "user", "content": "hello bm"}])
        self.assertEqual(st, 200, d)
        text = d["choices"][0]["message"]["content"]
        self.assertTrue(text.startswith("got ") and text.endswith("be short\n\nhello bm"), text)
        self.assertEqual(d["choices"][0]["finish_reason"], "stop")

    def test_turns_joined(self):
        st, d = self.chat([{"role": "user", "content": "one"}, {"role": "assistant", "content": "two"},
                           {"role": "user", "content": "three"}])
        self.assertEqual(st, 200, d)
        self.assertTrue(d["choices"][0]["message"]["content"].endswith("[assistant]\ntwo\n\n[user]\nthree"))

    def test_errors(self):
        st, d = self.chat([{"role": "robot", "content": "x"}])
        self.assertEqual(st, 400)
        st, d = self.chat([{"role": "user", "content": "please FAIL"}])
        self.assertEqual(st, 502)
        self.assertIn("quota exhausted", d["error"]["message"])
        st, d = self.chat([{"role": "user", "content": "x"}], token=True)
        self.assertEqual(st, 200)

    def test_gap(self):
        self.chat([{"role": "user", "content": "first"}])
        st, d = self.call("POST", "/v1/chat/completions", dict(messages=[{"role": "user", "content": "at once"}]))
        self.assertEqual(st, 429)

    def test_zz_log_has_no_text(self):
        log = read(os.path.join(self.dir, "usage.log"))
        self.assertIn('"status": 200', log)
        self.assertNotIn("hello bm", log)


if __name__ == "__main__":
    unittest.main(verbosity=1)
