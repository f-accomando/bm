#!/usr/bin/env python3
"""
tools/bm_net.py --config against a console on this PC that speaks the
transfer port as src/net/netxfer.c does (C: "key=value" lines, the answer
OK, the length, the settings after them; KV for a wrong line; BH from a
kernel that does not know C). The kernel's side of the same bytes is in
tests/net/test_netcon.c, its merge in tests/kernel/test_config.c.
Also --line against the network console's port (src/net/netcon.c): the
login's line saying what takes the keys, ':' + the line, its output shown
until "the line is done".
"""
import contextlib
import io
import os
import re
import socket
import struct
import sys
import threading
import time
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import bm_net  # noqa: E402

PASSWORD = "123456"
fails = 0


def check(ok, what):
    global fails
    print(("ok   " if ok else "FAIL ") + what)
    fails += not ok


class Console:
    """The transfer port of a console: settings in a dict, as config.c keeps them"""

    def __init__(self, knows_c=True):
        self.knows_c = knows_c
        self.settings = {"layout": "it", "wifi_ssid": "Casa", "github_token": "ghp_" + "x" * 36}
        self.got = []
        self.srv = socket.socket()
        self.srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.srv.bind(("127.0.0.1", 0))
        self.srv.listen(4)
        self.port = self.srv.getsockname()[1]
        threading.Thread(target=self.serve, daemon=True).start()

    def recv(self, c, n):
        data = b""
        while len(data) < n:
            chunk = c.recv(n - len(data))
            if not chunk:
                raise EOFError
            data += chunk
        return data

    def serve(self):
        while True:
            c, _ = self.srv.accept()
            try:
                self.one(c)
            except EOFError:
                pass
            c.close()

    def one(self, c):
        tag, op = self.recv(c, 4), self.recv(c, 1)
        if tag != b"BMXF" or op not in (b"S", b"P", b"K") + ((b"C",) if self.knows_c else ()):
            c.sendall(b"BH")
            return
        pw = self.recv(c, self.recv(c, 1)[0]).decode()
        path = self.recv(c, self.recv(c, 1)[0]).decode()
        size, crc = struct.unpack("<II", self.recv(c, 8))
        if pw != PASSWORD:
            c.sendall(b"PW")
            return
        c.sendall(b"OK")
        data = self.recv(c, size)
        if zlib.crc32(data) & 0xFFFFFFFF != crc:
            c.sendall(b"CE")
            return
        self.got.append((op, path, data))
        if op != b"C":
            c.sendall(b"OK")
            return
        changes = []
        for line in data.decode().splitlines():
            line = line.rstrip("\r ")
            if not line or line.startswith("#"):
                continue
            m = re.fullmatch(r"([A-Za-z0-9_]{1,23})=([^\x00-\x1f\x7f]{0,127})", line)
            if not m:
                c.sendall(b"KV")
                return
            changes.append(m.groups())
        for k, v in changes:
            if v:
                self.settings[k] = v
            else:
                self.settings.pop(k, None)
        text = "".join(f"{k}=(hidden, {len(v)} characters)\n" if re.search(r"(_token|_psk|_password|_key)$", k)
                       else f"{k}={v}\n" for k, v in self.settings.items()).encode()
        c.sendall(b"OK" + struct.pack("<I", len(text)) + text)


class NetConsole:
    """The network console's port as netcon.c speaks it: the greeting, the
    password, the login's line (what takes the keys, or a kernel's from
    before: the monitor's prompt), then the keys; a ':' line answered as the
    monitor does"""

    def __init__(self, focus):
        self.focus = focus
        self.keys = b""
        self.srv = socket.socket()
        self.srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.srv.bind(("127.0.0.1", 0))
        self.srv.listen(4)
        self.port = self.srv.getsockname()[1]
        threading.Thread(target=self.serve, daemon=True).start()

    def serve(self):
        while True:
            c, _ = self.srv.accept()
            try:
                self.one(c)
            except (EOFError, OSError):
                pass
            c.close()

    def one(self, c):
        c.sendall(b"bm v0.0.0-test network console\r\npassword: ")
        got = b""
        while b"\r\n" not in got:
            chunk = c.recv(64)
            if not chunk:
                raise EOFError
            got += chunk
        if got.split(b"\r\n")[0].decode() != PASSWORD:
            c.sendall(b"\r\nwrong password, bye\r\n")
            return
        if self.focus:
            c.sendall(b"\r\nok - Ctrl-Q leaves; the keys go to " + self.focus.encode() + b"\r\n")
        else:
            c.sendall(b"\r\nok - bm monitor, 'h' for help, Ctrl-Q to leave\r\n> ")
        while True:
            chunk = c.recv(256)
            if not chunk:
                return
            self.keys += chunk
            if self.keys.startswith(b":") and self.keys.endswith(b"\r"):
                line = self.keys[1:-1].decode()
                c.sendall(b"back to the monitor\r\n> :" + line.encode() + b"\r\n")
                for cmd in line.split(";"):
                    c.sendall(f"[{cmd.strip()}] done\r\n".encode())
                c.sendall(b"the line is done\r\n> ")


def run(console, *args, password=PASSWORD, job=("--config",)):
    bm_net.XFER_PORT = console.port
    out = io.StringIO()
    old = sys.argv
    sys.argv = ["bm_net.py", "127.0.0.1", "-p", password, *job, *args]
    try:
        with contextlib.redirect_stdout(out):
            rc = bm_net.main()
    finally:
        sys.argv = old
    return rc, out.getvalue()


def main():
    con = Console()

    rc, out = run(con)
    check(rc == 0 and "wifi_ssid=Casa" in out and "layout=it" in out, "--config alone shows the settings")
    check("github_token=(hidden, 40 characters)" in out and "ghp_" not in out, "  the token hidden")
    check(con.got[-1] == (b"C", "bm/config.txt", b"#\n"), "  a comment sent: nothing changes")

    token = "github_pat_" + "a" * 82
    rc, out = run(con, "github_token=" + token, "report_upload=0", "wifi_ssid=")
    check(rc == 0 and "settings changed: 3" in out, "three changes sent")
    check(con.got[-1][2] == f"github_token={token}\nreport_upload=0\nwifi_ssid=\n".encode(), "  as key=value lines")
    check(con.settings.get("github_token") == token and con.settings.get("report_upload") == "0" and
          "wifi_ssid" not in con.settings, "  the console has them")
    check("github_token=(hidden, 93 characters)" in out and token not in out, "  the new token not shown")

    n = len(con.got)
    for bad in ("no equals", "bad key=1", "x=" + "v" * 128, "=1", "x=a\tb"):
        rc, out = run(con, bad)
        check(rc == 1 and "is not key=value" in out, f"'{bad[:20]}' refused on the PC")
    check(len(con.got) == n, "  nothing sent")

    rc, out = run(con, "volume=3", password="000000")
    check(rc == 1 and "wrong password" in out, "a wrong code: wrong password")

    rc, out = run(con, job=("--send", __file__, "--name", "CHECK.PY", "--to", "/bm"))
    check(rc == 0 and con.got[-1][:2] == (b"S", "bm/CHECK.PY") and con.got[-1][2] == open(__file__, "rb").read(),
          "--send still the same (bm/CHECK.PY)")

    old = Console(knows_c=False)
    rc, out = run(old, "volume=3")
    check(rc == 1 and "newer kernel" in out, "a kernel without C: send it a newer one")

    # --line: a monitor line from the menu, the output until it is done
    menu = NetConsole("the menu (w a s d, Enter, Esc, 1-5 the tabs; q the monitor, or a line for it: :gpu; b3d; send)")
    t0 = time.time()
    rc, out = run(menu, job=("-P", str(menu.port), "--line", "gpu; b3d; send"))
    check(rc == 0 and menu.keys == b":gpu; b3d; send\r", "--line sends ':' + the line + Enter")
    check("[gpu] done" in out and "[send] done" in out and "the line is done" in out, "  its output shown")
    check(time.time() - t0 < 3, "  the login ends at the line saying what takes the keys (no 5 s wait)")
    app = NetConsole("the application (Ctrl-\\ back to the menu)")
    rc, out = run(app, job=("-P", str(app.port), "--line", "gpu"))
    check(rc == 2 and "back to the menu first" in out and app.keys == b"", "--line in a game: nothing sent")
    before = NetConsole(None)
    rc, out = run(before, job=("-P", str(before.port), "--line", "send"))
    check(rc == 0 and "does not say where the keys go" in out and "the line is done" in out,
          "--line on a kernel from before: said, still sent")

    if fails:
        print(f"bm_net --config, --line: {fails} FAILED")
        return 1
    print("bm_net --config, --line: all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
