#!/usr/bin/env python3
"""A bridge to the network console of a Pi: the owner types the password once, then a local client (tools/pi.py) can send a few
fixed operations without ever seeing the password.

  python3 tools/pi_bridge.py 192.168.1.108          # asks for the password, then waits (Ctrl-C to stop)
  python3 tools/pi.py line gpu                        # the client (another shell, or Claude)

Operations: line TEXT (a monitor line: gpu, b3d, ...), send FILE, play FILE, kernel FILE, shot OUT.png, config KEY=VALUE.. is NOT allowed.
FILE must be inside the repo or in ~/bmk*/ (the build folders); OUT of shot is a .png in a folder there. The socket is ~/.pi_bridge.sock, mode 0600."""
import getpass
import os
import socket
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
SOCK = os.path.expanduser("~/.pi_bridge.sock")
ALLOWED_DIRS = [ROOT] + [os.path.expanduser("~/" + d) for d in os.listdir(os.path.expanduser("~")) if d.startswith("bmk")]


def allowed(path):
    p = os.path.realpath(path)
    return os.path.isfile(p) and any(p.startswith(os.path.realpath(d) + os.sep) for d in ALLOWED_DIRS)


def allowed_dir(path):
    p = os.path.realpath(path)
    return os.path.isdir(p) and any(p == os.path.realpath(d) or p.startswith(os.path.realpath(d) + os.sep) for d in ALLOWED_DIRS)


def run(host, pw, req):
    op, _, arg = req.partition(" ")
    base = [sys.executable, os.path.join(HERE, "bm_net.py"), host]
    if op == "line" and arg and len(arg) < 200 and all(c.isalnum() or c in " _-.=" for c in arg):
        cmd = base + ["--line", arg]
    elif op in ("send", "play", "kernel") and allowed(arg):
        cmd = base + ["--" + op, os.path.realpath(arg)]
    elif op == "shot" and arg.lower().endswith(".png") and allowed_dir(os.path.dirname(arg) or "."):
        cmd = base + ["--shot", os.path.realpath(arg)]
    else:
        return "refused: %r" % req
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=300, env=dict(os.environ, BM_PASSWORD=pw))
    return (r.stdout + r.stderr).replace(pw, "***")


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "192.168.1.108"
    pw = os.environ.get("BM_PASSWORD") or getpass.getpass("console password of %s (stays in this process): " % host)
    if os.path.exists(SOCK):
        os.unlink(SOCK)
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.bind(SOCK)
    os.chmod(SOCK, 0o600)
    s.listen(1)
    print("bridge ready on", SOCK, "- Ctrl-C to stop")
    try:
        while True:
            c, _ = s.accept()
            req = c.makefile().readline().strip()
            print("request:", req)
            try:
                out = run(host, pw, req)
            except Exception as e:
                out = "error: %s" % e
            c.sendall(out.encode("utf-8", "replace"))
            c.close()
    finally:
        s.close()
        if os.path.exists(SOCK):
            os.unlink(SOCK)


if __name__ == "__main__":
    main()
