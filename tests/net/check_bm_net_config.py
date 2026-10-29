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
import shutil
import socket
import struct
import sys
import tempfile
import threading
import time
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import bm_net  # noqa: E402

PASSWORD = "123456"
FAKE_PNG = b"\x89PNG\r\n\x1a\n" + bytes((i * 7 + 3) & 0xFF for i in range(1992))   # 2000 bytes
# F: a frame of 4x2 pixels in RGB565 (red, green, blue, white; black, red, green, blue), as the console sends it
FRAME_PIXELS = [0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000, 0xF800, 0x07E0, 0x001F]
FAKE_FRAME = struct.pack("<HHBBH", 4, 2, 16, 0, 0) + b"".join(struct.pack("<H", v) for v in FRAME_PIXELS)
# V: frames of 4x2 pixels: a run of 4 red pixels and 4 literals; nothing changed; skip 1 and a literal pixel
VIEW_FRAMES = [
    struct.pack("<HH", 0x8004, 0xF800) + struct.pack("<H", 0x4004) + struct.pack("<4H", 0x07E0, 0x001F, 0xFFFF, 0x0000),
    b"",
    struct.pack("<HHH", 0x0001, 0x4001, 0x1234),
]
fails = 0


def check(ok, what):
    global fails
    print(("ok   " if ok else "FAIL ") + what)
    fails += not ok


class Console:
    """The transfer port of a console: settings in a dict, as config.c keeps them"""

    def __init__(self, knows_c=True, lose_answer=None, queues=False, busy=False, knows_tg=True, no_game=False,
                 knows_f=True):
        self.knows_c = knows_c
        self.knows_f = knows_f              # F (the page on the screen); else BH
        self.knows_tg = knows_tg            # T (screenshot) and G (file of bm/shots); else BH
        self.no_game = no_game              # T answered NG: no game drew a frame
        self.queues = queues                # S answered QD (written later, from the menu)
        self.busy = busy                    # S refused with BY after the header
        self.lose_answer = lose_answer      # a NetConsole: the kernel written, its answer lost
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
        if tag != b"BMXF" or op not in ((b"S", b"P", b"K") + ((b"C",) if self.knows_c else ())
                                        + ((b"T", b"G") if self.knows_tg else ())
                                        + ((b"F", b"V") if self.knows_f else ())):
            c.sendall(b"BH")
            return
        pw = self.recv(c, self.recv(c, 1)[0]).decode()
        path = self.recv(c, self.recv(c, 1)[0]).decode()
        size, crc = struct.unpack("<II", self.recv(c, 8))
        if pw != PASSWORD:
            c.sendall(b"PW")
            return
        if op == b"T":
            name = b"PONG0001.PNG"
            c.sendall(b"NG" if self.no_game else b"OK" + struct.pack("<I", len(name)) + name)
            return
        if op == b"V":                      # the stream: a first frame, a "nothing changed", one pixel changed, then closed
            c.sendall(b"OK")
            for payload in VIEW_FRAMES:
                c.sendall(struct.pack("<HHI", 4, 2, len(payload)) + payload)
            return
        if op == b"F":
            c.sendall(b"OK" + struct.pack("<II", len(FAKE_FRAME), zlib.crc32(FAKE_FRAME) & 0xFFFFFFFF) + FAKE_FRAME)
            return
        if op == b"G":
            p = path.lower()
            if not p.startswith("bm/shots/") or ".." in p:
                c.sendall(b"PA")
            elif p != "bm/shots/pong0001.png":
                c.sendall(b"NF")
            else:
                c.sendall(b"OK" + struct.pack("<II", len(FAKE_PNG), zlib.crc32(FAKE_PNG) & 0xFFFFFFFF) + FAKE_PNG)
            return
        if op == b"S" and self.busy:
            c.sendall(b"BY")            # a file sent before still waits to be written
            return
        c.sendall(b"OK")
        data = self.recv(c, size)
        if zlib.crc32(data) & 0xFFFFFFFF != crc:
            c.sendall(b"CE")
            return
        self.got.append((op, path, data))
        if op == b"K" and self.lose_answer:
            self.lose_answer.version = "v0.0.1-new"     # written, restarted: no answer reached the PC
            return
        if op == b"S" and self.queues:
            c.sendall(b"QD")            # kernels from 2026-10-10: written later, from the menu
            return
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
        self.version = "v0.0.0-test"
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
        c.sendall(b"bm " + self.version.encode() + b" network console\r\npassword: ")
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

    new = Console(queues=True)
    rc, out = run(new, job=("--send", __file__, "--name", "CHECK.PY", "--to", "/carts"))
    check(rc == 0 and "writes it from its menu" in out and new.got[-1][:2] == (b"S", "carts/CHECK.PY"),
          "--send to a newer kernel: QD, received, written from its menu")
    busy = Console(busy=True)
    rc, out = run(busy, job=("--send", __file__, "--name", "CHECK.PY"))
    check(rc == 1 and "busy" in out and not busy.got, "--send while a file still waits: BY, nothing sent")

    old = Console(knows_c=False)
    rc, out = run(old, "volume=3")
    check(rc == 1 and "newer kernel" in out, "a kernel without C: send it a newer one")

    # --shot (T) and --get (G): a PNG of bm/shots
    import tempfile
    tmp = tempfile.mkdtemp()
    png = os.path.join(tmp, "s.png")
    rc, out = run(con, job=("--shot", png))
    check(rc == 0 and open(png, "rb").read() == FAKE_PNG and "2000 bytes" in out, "--shot writes the screenshot")
    os.remove(png)
    rc, out = run(con, job=("--get", "BM/Shots/pong0001.png", png))
    check(rc == 0 and open(png, "rb").read() == FAKE_PNG, "--get writes the file of bm/shots")
    os.remove(png)
    rc, out = run(con, job=("--get", "bm/config.txt", png))
    check(rc == 1 and "path not allowed" in out and not os.path.exists(png), "--get outside bm/shots: PA")
    rc, out = run(con, job=("--get", "bm/shots/NOPE.PNG", png))
    check(rc == 1 and "no such file" in out and not os.path.exists(png), "--get of a missing file: NF")
    # --frame (F): the page on the screen now, a PPM; an older console answers BH
    ppm = os.path.join(tmp, "f.ppm")
    rc, out = run(con, job=("--frame", ppm))
    want = b"P6\n4 2\n255\n" + bytes([255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255,
                                       0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255])
    check(rc == 0 and os.path.exists(ppm) and open(ppm, "rb").read() == want, "--frame writes the frame as a PPM")
    os.remove(ppm)
    rc, out = run(Console(knows_f=False), job=("--frame", ppm))
    check(rc == 1 and not os.path.exists(ppm), "--frame to a console without F: BH, no file")
    # the stream (V): frames decoded, the cells that changed drawn
    import bm_watch
    bm_net.XFER_PORT = con.port
    got = [(list(px), ch) for px, ch, _n in bm_watch.stream_frames("127.0.0.1", PASSWORD, 4, 2, 100)]
    check(len(got) == 3 and got[0][0] == [0xF800] * 4 + [0x07E0, 0x001F, 0xFFFF, 0x0000] and got[0][1],
          "bm_watch: the first frame (a run, then literals)")
    check(got[1][0] == got[0][0] and not got[1][1], "bm_watch: a frame with no tokens changes nothing")
    check(got[2][0] == [0xF800, 0x1234] + [0xF800] * 2 + [0x07E0, 0x001F, 0xFFFF, 0x0000] and got[2][1],
          "bm_watch: skip one pixel, a literal one")
    sx = bm_watch.render_sixel([0xF800] * 8 + [0x001F] * 8, None, 4, 4, 1)
    check(sx.startswith(chr(27) + "[1;1H" + chr(27) + "P0;1;0q\"1;1;4;4#") and sx.endswith(chr(27) + chr(92)) and "#224!4B" in sx and "#3!4K" in sx and
          sx.count("-") == 0, "bm_watch sixel: a 4x4 image, one band, red rows 0-1, blue rows 2-3")
    band2 = bm_watch.render_sixel([1] * 4 * 12, None, 4, 12, 1)
    moved = [1] * 4 * 12
    moved[11 * 4] = 2
    delta2 = bm_watch.render_sixel(moved, [1] * 4 * 12, 4, 12, 1)
    bands = re.sub(r"#[0-9]+;2;[0-9]+;[0-9]+;[0-9]+", "", delta2.split("q\"1;1;4;12", 1)[1]).rstrip("\\").split("-")
    check(band2.count("-") == 1 and len(bands) == 2 and bands[0] == "" and bands[1] != "",
          "bm_watch sixel: a band with nothing new is left empty, the one that changed has its pixels")
    first = bm_watch.render(got[0][0], None, 4, 2)
    check(first.count("▀") == 4 and "38;2;255;0;0;48;2;0;255;0" in first and "[1;1H" in first,
          "bm_watch: the first picture draws every cell (red above green)")
    delta = bm_watch.render(got[2][0], got[0][0], 4, 2)
    check(delta.count("▀") == 1 and "[1;2H" in delta, "bm_watch: only the cell that changed is drawn")
    check(bm_watch.render(got[1][0], got[0][0], 4, 2) == "", "bm_watch: no change, nothing drawn")
    try:
        bm_net.XFER_PORT = Console(knows_f=False).port
        list(bm_watch.stream_frames("127.0.0.1", PASSWORD, 4, 2, 100))
        check(False, "bm_watch: a console without V is refused")
    except RuntimeError as e:
        check("BH" in str(e) or "bad request" in str(e), "bm_watch: a console without V is refused")
    rc, out = run(Console(no_game=True), job=("--shot", png))
    check(rc == 1 and "no game drew a frame" in out and not os.path.exists(png), "--shot with no game: NG")
    rc, out = run(Console(knows_tg=False), job=("--shot", png))
    check(rc == 2 and "kernel too old" in out, "--shot on a kernel without T/G: too old")

    # --line: a monitor line from the menu, the output until it is done
    menu = NetConsole("the menu (w a s d, Enter, Esc, 1-4 the tabs; q the monitor, or a line for it: :crc; b3d; send)")
    t0 = time.time()
    rc, out = run(menu, job=("-P", str(menu.port), "--line", "crc; b3d; send"))
    check(rc == 0 and menu.keys == b":crc; b3d; send\r", "--line sends ':' + the line + Enter")
    check("[crc] done" in out and "[send] done" in out and "the line is done" in out, "  its output shown")
    check(time.time() - t0 < 3, "  the login ends at the line saying what takes the keys (no 5 s wait)")
    app = NetConsole("the application (Ctrl-\\ back to the menu)")
    rc, out = run(app, job=("-P", str(app.port), "--line", "crc"))
    check(rc == 2 and "back to the menu first" in out and app.keys == b"", "--line in a game: nothing sent")
    # a kernel whose answer is lost (the RGB30 dropped its WiFi after a long
    # SD write): what the console runs afterwards says whether it went
    net = NetConsole("the menu (test)")
    lost = Console(lose_answer=net)
    bm_net.PORT = net.port
    rc, out = run(lost, job=("--kernel", __file__))
    check(rc == 0 and "the answer was lost" in out and "running bm v0.0.1-new" in out,
          "--kernel without an answer: the console's new version found")
    bm_net.PORT = 3333

    before = NetConsole(None)
    rc, out = run(before, job=("-P", str(before.port), "--line", "send"))
    check(rc == 0 and "does not say where the keys go" in out and "the line is done" in out,
          "--line on a kernel from before: said, still sent")

    # --keys: the keys one at a time, in order; nothing in a game
    bm_net.KEYS_PAUSE, bm_net.KEYS_READ = 0.05, 0.3
    kc = NetConsole("the menu (test)")
    rc, out = run(kc, job=("-P", str(kc.port), "--keys", "Xa"))
    check(rc == 0 and kc.keys == b"Xa", "--keys Xa: X then a, in this order")
    app = NetConsole("the application (Esc back to the menu)")
    rc, out = run(app, job=("-P", str(app.port), "--keys", "Xa"))
    check(rc == 2 and "back to the menu first" in out and app.keys == b"", "--keys in a game: exit 2, nothing sent")
    rc, out = run(kc, job=("-P", str(kc.port), "--keys", "X a"))
    check(rc == 1 and kc.keys == b"Xa", "--keys with a space: refused on the PC")

    # --game-exit: the Ctrl-\ byte, in a game only
    rc, out = run(app, job=("-P", str(app.port), "--game-exit"))
    check(rc == 0 and app.keys == b"", "--game-exit in a game: the one byte 0x1C")
    kc.keys = b""
    rc, out = run(kc, job=("-P", str(kc.port), "--game-exit"))
    check(rc == 2 and "game only" in out and kc.keys == b"", "--game-exit in the menu: exit 2, nothing sent")

    # the bridge: names only, from the fixed table, through bm_net --keys, password hidden
    import pi_bridge
    seen = []

    class R:
        stdout, stderr, returncode = "pw=s3cret ok", "", 0

    real = pi_bridge.subprocess.run
    pi_bridge.subprocess.run = lambda cmd, **kw: (seen.append(cmd), R())[1]
    try:
        out = pi_bridge.run("1.2.3.4", "s3cret", "keys crash-dabt")
        check(seen and seen[-1][-2:] == ["--keys", "Xa"] and "s3cret" not in out, "bridge: keys crash-dabt -> --keys Xa, password hidden")
        for name, seq in (("monitor", "q"), ("crash-undef", "Xu"), ("crash-prefetch", "Xb")):
            pi_bridge.run("1.2.3.4", "s3cret", "keys " + name)
            check(seen[-1][-2:] == ["--keys", seq], f"bridge: keys {name} -> {seq}")
        for name, f in (("bootprev", "bootprev.txt"), ("bootlog", "bootlog.txt"), ("lastrun", "lastrun.txt")):
            pi_bridge.run("1.2.3.4", "s3cret", "logs " + name)
            check(seen[-1][-3] == "--get" and seen[-1][-2] == "bm/" + f, f"bridge: logs {name} -> --get bm/{f}")
        pi_bridge.run("1.2.3.4", "s3cret", "logs bat00")
        check(seen[-1][-3] == "--get" and seen[-1][-2] == "bm/bat00.csv", "bridge: logs bat00 -> --get bm/bat00.csv")
        n = len(seen)
        pi_bridge.run("1.2.3.4", "s3cret", "logs bat0")
        pi_bridge.run("1.2.3.4", "s3cret", "logs bat00x")
        check(len(seen) == n, "bridge: logs bat0 and bat00x are refused before any child starts")
        def put_log(cmd, **kw):         # bm_net --get wrote the log, the password in it
            seen.append(cmd)
            with open(cmd[-1], "w") as f:
                f.write("boot net_password=s3cret end")
            return R()
        pi_bridge.subprocess.run = put_log
        out = pi_bridge.run("1.2.3.4", "s3cret", "logs bootprev")
        with open(os.path.join(pi_bridge.ROOT, "out", "pi_bootprev.txt")) as f:
            kept = f.read()
        check("s3cret" not in out and "net_password=***" in out and "s3cret" not in kept,
              "bridge: logs bootprev: the password masked in the answer and in the file kept")
        pi_bridge.subprocess.run = lambda cmd, **kw: (seen.append(cmd), R())[1]
        for name in ("freeze", "reboot", "crash-dabt"):
            pi_bridge.run("1.2.3.4", "s3cret", "keys " + name)
            check(seen[-1][-3] == "--in-monitor" and seen[-1][-2] == "--keys", f"bridge: keys {name} only when the monitor has the keys")
        check(seen and pi_bridge.run("1.2.3.4", "s3cret", "keys freeze") is not None and seen[-1][-1] == "Xf", "bridge: keys freeze -> Xf")
        pi_bridge.run("1.2.3.4", "s3cret", "keys reboot")
        check(seen[-1][-3:] == ["--in-monitor", "--keys", "r"], "bridge: keys reboot -> r")
        pi_bridge.run("1.2.3.4", "s3cret", "keys monitor")
        check("--in-monitor" not in seen[-1], "bridge: keys monitor needs no monitor")
        pi_bridge.run("1.2.3.4", "s3cret", "keys game-exit")
        check(seen[-1][-1] == "--game-exit" and "--keys" not in seen[-1], "bridge: keys game-exit -> --game-exit")

        # config: allowed keys go through, secret keys are refused before any child starts, the log never holds a value
        pi_bridge.run("1.2.3.4", "s3cret", "config menu_scale=3")
        check(seen[-1][-2:] == ["--config", "menu_scale=3"], "bridge: config menu_scale=3 goes through")
        n = len(seen)
        out = pi_bridge.run("1.2.3.4", "s3cret", "config net_password=x")
        check(len(seen) == n and "secret keys are not set" in out, "bridge: config net_password=x refused, child not started")
        out = pi_bridge.run("1.2.3.4", "s3cret", "config menu_scale=3 WIFI_PSK=x")
        check(len(seen) == n and "secret keys are not set" in out, "bridge: config wifi_psk=x (bm_net.SECRET) refused, child not started")
        logged = pi_bridge.masked("config menu_scale=3 layout=it")
        check("***" in logged and "=3" not in logged and "=it" not in logged, "bridge: the log line of a config holds *** and not the value")

        # line: only the monitor commands of the list, ';' between them, b3d with tests=/profiles= (R-NET6)
        for req, want in (("crc", "crc"), ("err", "err"), ("crc", "crc"),
                          ("crc; log; reboot", "crc; log; reboot"),
                          ("crc; b3d tests=match,quad_tex profiles=GPU,FS2; send", "crc; b3d tests=match,quad_tex profiles=GPU,FS2; send"),
                          ("  crc ;b3d   tests=quad_flat  profiles=FS2 ;  send ", "crc; b3d tests=quad_flat profiles=FS2; send")):
            pi_bridge.run("1.2.3.4", "s3cret", "line " + req)
            check(seen[-1][-2:] == ["--line", want], f"bridge: line {req!r} -> {want!r}")
        out = pi_bridge.run("1.2.3.4", "s3cret", "line set wifi_psk=x")
        check(out.startswith("refused: line:") and "'set' is not allowed" in out, "bridge: line set wifi_psk=x refused, says why")
        n0 = len(seen)
        for req in ("save", "play pong", "crc; set wifi_psk=x", "crc; save", "err; play pong", "CRC", "Crc",
                    "crc\tb3d", "crc\rsave", "crc\x1b", "err²", "crc;;err", "crc; ", "err now", "b3d tests=a-b", "b3d verbose=1",
                    "b3d tests=", "x" * 200, "; crc", "crc x", "crc,err", "", "crc;", "b3d tests=a.b", "b3d foo=1"):
            out = pi_bridge.run("1.2.3.4", "s3cret", "line " + req)
            check(out.startswith("refused: line:"), f"bridge: line {req[:30]!r} refused")
        check(len(seen) == n0, "bridge: the refused lines started nothing")

        # the password: BM_PASSWORD, then a file of the owner (mode 0600, one line), then the prompt; the value never shown
        pwdir = tempfile.mkdtemp(prefix="pipw-")
        try:
            pwf = os.path.join(pwdir, "pw")
            asked = []
            ask = lambda prompt: (asked.append(prompt), "typed-pw")[1]
            with open(pwf, "w") as f:
                f.write("file-pw\r\nsecond line\n")
            os.chmod(pwf, 0o600)
            pw, where, why = pi_bridge.get_password("h", {"BM_PASSWORD_FILE": pwf}, ask)
            check(pw == "file-pw" and why is None and pwf in where and not asked and "file-pw" not in where,
                  "password: the file (mode 0600), first line only, CR stripped, the prompt not used, value not in the source text")
            pw, where, why = pi_bridge.get_password("h", {"BM_PASSWORD": "env-pw", "BM_PASSWORD_FILE": pwf}, ask)
            check(pw == "env-pw" and where == "BM_PASSWORD" and not asked, "password: BM_PASSWORD wins over the file")
            os.chmod(pwf, 0o644)
            pw, where, why = pi_bridge.get_password("h", {"BM_PASSWORD_FILE": pwf}, ask)
            check(pw is None and why and "password file mode" in why and "file-pw" not in why and not asked,
                  "password: a file others can read is refused, the value not in the message, no fallback to the prompt")
            os.chmod(pwf, 0o600)
            with open(pwf, "w") as f:
                f.write("\n")
            pw, where, why = pi_bridge.get_password("h", {"BM_PASSWORD_FILE": pwf}, ask)
            check(pw is None and why and "empty" in why, "password: an empty file is refused")
            pw, where, why = pi_bridge.get_password("h", {"BM_PASSWORD_FILE": os.path.join(pwdir, "none")}, ask)
            check(pw == "typed-pw" and where == "the prompt" and len(asked) == 1, "password: no file -> the prompt")
        finally:
            shutil.rmtree(pwdir, ignore_errors=True)

        # status: down without a process; up with the version and the state of the run before, the password masked
        real_cv = pi_bridge.bm_net.console_version
        pi_bridge.bm_net.console_version = lambda host: None
        n0 = len(seen)
        out = pi_bridge.run("1.2.3.4", "s3cret", "status")
        check(out.startswith("status: down") and len(seen) == n0, "bridge: status of a Pi that does not answer: down, nothing run")
        pi_bridge.bm_net.console_version = lambda host: "v9"

        def put_prev(cmd, **kw):
            seen.append(cmd)
            with open(cmd[-1], "w") as f:
                f.write("x" + chr(10) + "--- the run before ended in a crash while: t, 12 s after boot, kernel k; the last lines it printed (RAM) ---"
                        + chr(10) + "net_password=s3cret")
            return R()
        pi_bridge.subprocess.run = put_prev
        out = pi_bridge.run("1.2.3.4", "s3cret", "status")
        check("status: up - bm v9" in out and "previous run: ended in a crash while: t, 12 s after boot, kernel k" in out
              and "checked: 20" in out and "s3cret" not in out, "bridge: status of a Pi that answers: version, previous run, time, password masked")
        pi_bridge.bm_net.console_version = real_cv

        def slow(cmd, **kw):
            raise pi_bridge.subprocess.TimeoutExpired(cmd, 300)
        pi_bridge.subprocess.run = slow
        check(pi_bridge.run("1.2.3.4", "s3cret", "line err").startswith("timeout:"), "bridge: a request with no end: timeout message")
        pi_bridge.subprocess.run = lambda cmd, **kw: (seen.append(cmd), R())[1]
        n = len(seen)
        for bad in ("status x", "logs foo", "logs ../x", "logs BOOTPREV.TXT", "logs", "keys foo", "keys Xa", "keys", "keys crash-freeze", "keys monitor Xa"):
            check(pi_bridge.run("1.2.3.4", "s3cret", bad).startswith("refused"), f"bridge: '{bad}' refused")
        check(len(seen) == n, "  nothing run")
    finally:
        pi_bridge.subprocess.run = real

    # --in-monitor: the keys only where the monitor has them
    mc = NetConsole("the menu (test)")
    rc, out = run(mc, job=("-P", str(mc.port), "--in-monitor", "--keys", "Xf"))
    check(rc == 2 and mc.keys == b"" and "monitor only" in out, "--in-monitor --keys in the menu: exit 2, nothing sent")
    mo = NetConsole("the monitor ('h' for help, M the menu)")
    rc, out = run(mo, job=("-P", str(mo.port), "--in-monitor", "--keys", "Xf"))
    check(rc == 0 and mo.keys == b"Xf", "--in-monitor --keys in the monitor: Xf pressed")

    # one bridge only: a live socket is respected, a stale file is removed
    import tempfile
    sock_path = os.path.join(tempfile.mkdtemp(prefix="bm-sock-"), "b.sock")
    first = pi_bridge.claim_socket(sock_path)
    try:
        pi_bridge.claim_socket(sock_path)
        check(False, "bridge: a second one on the same socket is refused")
    except SystemExit as e:
        check("already running" in str(e), "bridge: a second one on the same socket is refused")
    first.close()
    second = pi_bridge.claim_socket(sock_path)
    check(second is not None, "bridge: a stale socket file is taken over")
    second.close()

    # a console that is off: a message and exit 3, no traceback
    free = socket.socket()
    free.bind(("127.0.0.1", 0))
    dead_port = free.getsockname()[1]
    free.close()
    import subprocess
    r = subprocess.run([sys.executable, os.path.join(os.path.dirname(bm_net.__file__), "bm_net.py"), "127.0.0.1", "-P", str(dead_port),
                        "-p", "x", "--line", "err"], capture_output=True, text=True, timeout=60)
    check(r.returncode == 3 and "does not answer" in r.stdout and "Traceback" not in r.stderr,
          "a console that is off: exit 3, 'does not answer', no traceback")

    if fails:
        print(f"bm_net --config, --line: {fails} FAILED")
        return 1
    print("bm_net --config, --line: all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
