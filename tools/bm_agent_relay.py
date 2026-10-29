#!/usr/bin/env python3
"""
bm_agent_relay.py - the console's way to an AI subscription (ai_ask, docs/api/system.md).

A Claude, ChatGPT or Google subscription works only through the provider's own program
(Claude Code, Codex, Gemini CLI), signed in by you on your own PC or server. The console
cannot run those programs. This relay runs on that PC: it serves the same API as the free
providers (POST /v1/chat/completions, OpenAI-compatible) over HTTPS on your LAN, and answers
each request with one run of the official program, unmodified, under your own login.

  bm_agent_relay.py cert --ip 192.168.1.20      # once: a CA, a certificate and a token
  bm_agent_relay.py serve --backend claude      # then: serve on port 8443

`cert` prints the three lines for bm/config.txt and the CA to add at the end of
bm/ca.pem on the SD card. The relay never reads, stores or sends your provider login:
the official program does its own sign-in. Keep the relay for your own use. A subscription
covers one person's ordinary use: do not share the token, the address or the answers'
quota with other people (Anthropic's and OpenAI's terms; docs/design/ai-agent.md).

Backends (the prompt goes on stdin, never through a shell):
  claude   claude -p --output-format json --tools "" --no-session-persistence
  codex    codex exec --skip-git-repo-check --sandbox read-only --output-last-message FILE -
  gemini   gemini (the prompt on stdin)
  cmd      --cmd 'PROGRAM ARG ...': any program that reads the prompt and prints the answer
The tools of the agent programs are off (claude) or read-only in an empty folder (codex):
the console's agent writes files itself, after you confirm them.

Python 3.8+, standard library only. `cert` needs the openssl program.
"""
import argparse
import hmac
import http.server
import json
import os
import re
import secrets
import shlex
import shutil
import socketserver
import ssl
import subprocess
import sys
import tempfile
import threading
import time

PROMPT_MAX = 256 * 1024          # bytes of the joined messages (the kernel sends at most 192 KB)
RUN_TIMEOUT = 170                # s: the kernel waits at most 180 s for a read (AI_TIMEOUT_LIMIT)
MODEL_RE = re.compile(r"^[A-Za-z0-9._:/@-]{1,96}$")
DEFAULT_DIR = os.path.join(os.path.expanduser("~"), ".bm-relay")


# ---- the certificate and the token ----

def run_openssl(args, cwd):
    subprocess.run(["openssl"] + args, cwd=cwd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)


def make_cert(d, ips, names):
    """A CA (P-256, 10 years) and a server certificate for ips and names (P-256, 825 days), as the
    kernel's mbedTLS accepts them; the token in d/token. Returns the CA's PEM text."""
    os.makedirs(d, mode=0o700, exist_ok=True)
    if not shutil.which("openssl"):
        sys.exit("cert: the openssl program is needed")
    san = ",".join(["IP:" + i for i in ips] + ["DNS:" + n for n in names])
    with open(os.path.join(d, "server.ext"), "w") as f:
        f.write("basicConstraints=CA:FALSE\nkeyUsage=digitalSignature\nextendedKeyUsage=serverAuth\n"
                "subjectAltName=%s\n" % san)
    if not os.path.exists(os.path.join(d, "ca.key")):
        run_openssl(["ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", "ca.key"], d)
        run_openssl(["req", "-x509", "-new", "-key", "ca.key", "-sha256", "-days", "3650", "-subj",
                     "/CN=bm agent relay CA", "-addext", "basicConstraints=critical,CA:TRUE",
                     "-addext", "keyUsage=critical,keyCertSign,cRLSign", "-out", "ca.pem"], d)
    run_openssl(["ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", "server.key"], d)
    run_openssl(["req", "-new", "-key", "server.key", "-subj", "/CN=bm agent relay", "-out", "server.csr"], d)
    run_openssl(["x509", "-req", "-in", "server.csr", "-CA", "ca.pem", "-CAkey", "ca.key", "-CAcreateserial",
                 "-days", "825", "-sha256", "-extfile", "server.ext", "-out", "server.pem"], d)
    for n in ("ca.key", "server.key"):
        os.chmod(os.path.join(d, n), 0o600)
    tok = os.path.join(d, "token")
    if not os.path.exists(tok):
        with open(os.open(tok, os.O_WRONLY | os.O_CREAT, 0o600), "w") as f:
            f.write("bmr-" + secrets.token_urlsafe(32) + "\n")
    with open(os.path.join(d, "ca.pem")) as f:
        return f.read()


def read_token(d):
    with open(os.path.join(d, "token")) as f:
        return f.read().strip()


# ---- the backends: one run of the official program per request ----

def join_messages(msgs):
    """The system text and the conversation as one prompt (the CLIs take one prompt)."""
    system = "\n\n".join(m["content"] for m in msgs if m["role"] == "system")
    turns = [m for m in msgs if m["role"] != "system"]
    if len(turns) == 1:
        return system, turns[0]["content"]
    lines = ["The conversation so far. Answer the last user message only, as the assistant."]
    for m in turns:
        lines.append("\n[%s]\n%s" % (m["role"], m["content"]))
    return system, "\n".join(lines)


class BackendError(Exception):
    def __init__(self, status, message):
        super().__init__(message)
        self.status = status


def run(argv, prompt, cwd, timeout):
    try:
        p = subprocess.run(argv, input=prompt.encode(), cwd=cwd, capture_output=True, timeout=timeout)
    except FileNotFoundError:
        raise BackendError(502, "the program %s is not installed on the relay's PC" % argv[0])
    except subprocess.TimeoutExpired:
        raise BackendError(504, "the program took more than %d s" % timeout)
    out, err = p.stdout.decode(errors="replace"), p.stderr.decode(errors="replace")
    if p.returncode != 0:
        tail = (err.strip() or out.strip()).splitlines()[-1:] or ["no message"]
        raise BackendError(502, "%s ended with code %d: %s" % (argv[0], p.returncode, tail[0][:300]))
    return out


def ask_claude(system, prompt, model, cwd, timeout):
    argv = ["claude", "-p", "--output-format", "json", "--tools", "", "--no-session-persistence"]
    if system:
        argv += ["--system-prompt", system]
    if model:
        argv += ["--model", model]
    out = run(argv, prompt, cwd, timeout)
    try:
        j = json.loads(out)
    except ValueError:
        raise BackendError(502, "claude printed no JSON")
    if j.get("is_error"):
        raise BackendError(429 if "limit" in str(j.get("result", "")).lower() else 502, str(j.get("result"))[:300])
    u = j.get("usage") or {}
    return str(j.get("result", "")), u.get("input_tokens"), u.get("output_tokens")


def ask_codex(system, prompt, model, cwd, timeout):
    last = os.path.join(cwd, "last.txt")
    argv = ["codex", "exec", "--skip-git-repo-check", "--sandbox", "read-only", "--output-last-message", last]
    if model:
        argv += ["--model", model]
    run(argv + ["-"], (system + "\n\n" + prompt) if system else prompt, cwd, timeout)
    try:
        with open(last, encoding="utf-8", errors="replace") as f:
            return f.read().strip(), None, None
    except OSError:
        raise BackendError(502, "codex wrote no last message")


def ask_gemini(system, prompt, model, cwd, timeout):
    argv = ["gemini"] + (["--model", model] if model else [])
    return run(argv, (system + "\n\n" + prompt) if system else prompt, cwd, timeout).strip(), None, None


def make_cmd(cmd):
    argv = shlex.split(cmd)

    def ask(system, prompt, model, cwd, timeout):
        return run(argv, (system + "\n\n" + prompt) if system else prompt, cwd, timeout).strip(), None, None
    return ask


BACKENDS = {"claude": ask_claude, "codex": ask_codex, "gemini": ask_gemini}


# ---- the server ----

class Relay:
    def __init__(self, backend, ask, token, gap, log_path):
        self.backend, self.ask, self.token, self.gap, self.log_path = backend, ask, token, gap, log_path
        self.busy = threading.Lock()
        self.last = 0.0

    def log(self, **kv):
        """One line per request: time, backend, sizes, seconds and status. Never the text."""
        if self.log_path:
            with open(self.log_path, "a") as f:
                f.write(json.dumps(dict(t=time.strftime("%Y-%m-%dT%H:%M:%S"), backend=self.backend, **kv)) + "\n")


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "bm-agent-relay"

    def log_message(self, *a):
        pass

    def reply(self, status, obj):
        body = json.dumps(obj).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def fail(self, status, message):
        self.reply(status, {"error": {"message": message, "type": "relay_error", "code": status}})

    def authorized(self):
        got = self.headers.get("Authorization", "")
        want = "Bearer " + self.server.relay.token
        return hmac.compare_digest(got.encode(), want.encode())

    def do_GET(self):
        if self.path == "/health":
            return self.reply(200, {"ok": True, "backend": self.server.relay.backend})
        if not self.authorized():
            return self.fail(401, "wrong or missing token (ai_key in bm/config.txt)")
        if self.path == "/v1/models":
            return self.reply(200, {"object": "list", "data": [{"id": self.server.relay.backend, "object": "model"}]})
        self.fail(404, "not here")

    def do_POST(self):
        relay = self.server.relay
        try:
            n = int(self.headers.get("Content-Length") or 0)
        except ValueError:
            n = -1
        if n < 0 or n > PROMPT_MAX + 65536:
            self.close_connection = True
            return self.fail(413, "a bad length, or a request over %d KB" % (PROMPT_MAX // 1024))
        raw = self.rfile.read(n)
        if self.path.rstrip("/") not in ("/v1/chat/completions", "/chat/completions"):
            return self.fail(404, "not here")
        if not self.authorized():
            return self.fail(401, "wrong or missing token (ai_key in bm/config.txt)")
        try:
            req = json.loads(raw)
            msgs = req["messages"]
            assert isinstance(msgs, list) and msgs
            for m in msgs:
                assert m.get("role") in ("system", "user", "assistant") and isinstance(m.get("content"), str)
        except (ValueError, KeyError, TypeError, AssertionError):
            return self.fail(400, "the body is not a chat request")
        model = req.get("model") or ""
        if model in ("default", relay.backend):
            model = ""                      # the program's own default model
        if model and not MODEL_RE.match(model):
            return self.fail(400, "a bad model name")
        system, prompt = join_messages(msgs)
        if len(system.encode()) + len(prompt.encode()) > PROMPT_MAX:
            return self.fail(413, "the messages are over %d KB" % (PROMPT_MAX // 1024))
        if not relay.busy.acquire(blocking=False):
            return self.fail(429, "the relay answers one request at a time: try again")
        t0 = time.time()
        try:
            wait = relay.gap - (t0 - relay.last)
            if wait > 0:
                return self.fail(429, "the relay waits %.0f s between two requests" % relay.gap)
            relay.last = t0
            with tempfile.TemporaryDirectory(prefix="bm-relay-") as cwd:   # an empty folder: nothing to read
                text, pin, pout = relay.ask(system, prompt, model, cwd, RUN_TIMEOUT)
        except BackendError as e:
            relay.log(chars=len(prompt), seconds=round(time.time() - t0, 1), status=e.status)
            return self.fail(e.status, str(e))
        finally:
            relay.busy.release()
        relay.log(chars=len(prompt), seconds=round(time.time() - t0, 1), status=200)
        usage = {}
        if pin is not None:
            usage = {"prompt_tokens": pin, "completion_tokens": pout}
        self.reply(200, {"id": "bmr-" + secrets.token_hex(6), "object": "chat.completion", "created": int(t0),
                         "model": model or relay.backend,
                         "choices": [{"index": 0, "message": {"role": "assistant", "content": text},
                                      "finish_reason": "stop"}],
                         "usage": usage})


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True


def serve(a):
    d = a.dir
    if a.backend == "cmd":
        if not a.cmd:
            sys.exit("serve: --backend cmd needs --cmd 'PROGRAM ARG ...'")
        ask = make_cmd(a.cmd)
    else:
        ask = BACKENDS[a.backend]
        if not shutil.which(a.backend):
            print("warning: %s is not installed (or not on PATH): every request will fail" % a.backend, file=sys.stderr)
    try:
        token = read_token(d)
    except OSError:
        sys.exit("serve: no token in %s: run `bm_agent_relay.py cert --ip <this PC's address>` first" % d)
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.minimum_version = ssl.TLSVersion.TLSv1_2
    ctx.load_cert_chain(os.path.join(d, "server.pem"), os.path.join(d, "server.key"))
    srv = Server((a.bind, a.port), Handler)
    srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
    srv.relay = Relay(a.backend, ask, token, a.gap, None if a.no_log else os.path.join(d, "usage.log"))
    print("bm agent relay: https://%s:%d/v1 (backend %s). Your own use only: keep the token private."
          % (a.bind, srv.server_address[1], a.backend), flush=True)
    if a.port_file:
        with open(a.port_file, "w") as f:
            f.write(str(srv.server_address[1]))
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="what", required=True)
    c = sub.add_parser("cert", help="make the CA, the certificate and the token")
    c.add_argument("--ip", action="append", default=[], help="the relay PC's LAN address (repeat for more)")
    c.add_argument("--name", action="append", default=[], help="a host name of the PC (optional)")
    c.add_argument("--dir", default=DEFAULT_DIR)
    c.add_argument("--port", type=int, default=8443)
    s = sub.add_parser("serve", help="serve /v1/chat/completions")
    s.add_argument("--backend", choices=sorted(BACKENDS) + ["cmd"], default="claude")
    s.add_argument("--cmd", help="for --backend cmd: the program and its arguments")
    s.add_argument("--bind", default="0.0.0.0")
    s.add_argument("--port", type=int, default=8443)
    s.add_argument("--gap", type=float, default=2.0, help="seconds between two requests (default 2)")
    s.add_argument("--dir", default=DEFAULT_DIR)
    s.add_argument("--no-log", action="store_true", help="no usage.log")
    s.add_argument("--port-file", help=argparse.SUPPRESS)
    a = ap.parse_args()
    if a.what == "cert":
        if not a.ip and not a.name:
            sys.exit("cert: give --ip (the address the console uses to reach this PC)")
        ca = make_cert(a.dir, a.ip, a.name)
        host = (a.ip or a.name)[0]
        print("Put these lines in bm/config.txt on the SD card:\n")
        print("ai_url=https://%s:%d/v1" % (host, a.port))
        print("ai_model=default")
        print("ai_key=%s\n" % read_token(a.dir))
        print("Then add this CA at the end of bm/ca.pem on the SD card (keep the lines that are there):\n")
        print(ca)
        return
    serve(a)


if __name__ == "__main__":
    main()
