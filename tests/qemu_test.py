#!/usr/bin/env python3
"""
End-to-end tests in QEMU (-M raspi0). Images are loaded at 0x8000 through
-bios, exactly as the Pi firmware does. The serial port is a TCP socket.

  tests/qemu_test.py [--build build] [--update-ref] [-k name]
"""
import argparse
import hashlib
import re
import os
import socket
import subprocess
import sys
import tempfile
import time
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, os.path.join(HERE, "..", "scripts"))
import bm33_load  # noqa: E402
import mkb33  # noqa: E402

QEMU = os.environ.get("QEMU", "qemu-system-arm")
REF_DIR = os.path.join(HERE, "ref")
PROMPT = b"type 'h' for help"
DEMO = b"s32: playing"
B33_DEMO = b"native demo cart"


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Qemu:
    def __init__(self, image):
        self.tmp = tempfile.mkdtemp(prefix="bm33-")
        self.mon_path = os.path.join(self.tmp, "mon.sock")
        tcp = free_port()
        self.proc = subprocess.Popen(
            [QEMU, "-M", "raspi0", "-bios", image, "-display", "none",
             "-serial", f"tcp:127.0.0.1:{tcp},server=on,wait=on",
             "-serial", "null",
             "-monitor", f"unix:{self.mon_path},server=on,wait=off"],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        self.port = bm33_load.Port(f"tcp:127.0.0.1:{tcp}", 115200)
        self.buf = b""

    # file-like sink so bm33_load.Loader can echo into our buffer
    def write(self, data):
        self.buf += data

    def flush(self):
        pass

    def expect(self, needle, timeout=10.0):
        if isinstance(needle, str):
            needle = needle.encode()
        deadline = time.time() + timeout
        while needle not in self.buf:
            if time.time() > deadline:
                raise AssertionError(
                    f"timeout waiting for {needle!r}; serial output:\n"
                    + self.buf.decode(errors="replace"))
            self.buf += self.port.read(0.1)
        i = self.buf.index(needle) + len(needle)
        seen, self.buf = self.buf[:i], self.buf[i:]
        return seen

    def boot(self, skip_demo=True):
        """Waits for the monitor prompt, skipping the boot animation demo."""
        out = self.expect(DEMO, timeout=20)
        if skip_demo:
            self.send("q")
        out += self.expect(B33_DEMO, timeout=20)
        if skip_demo:
            self.send("q")
        out += self.expect(PROMPT, timeout=40)
        out += self.expect("> ")
        return out

    def send(self, data):
        self.port.write(data.encode() if isinstance(data, str) else data)

    def monitor(self, cmd):
        with socket.socket(socket.AF_UNIX) as s:
            s.connect(self.mon_path)
            s.sendall(cmd.encode() + b"\n")
            time.sleep(0.5)

    def screendump(self):
        path = os.path.join(self.tmp, "screen.ppm")
        self.monitor(f"screendump {path}")
        for _ in range(50):
            if os.path.exists(path) and os.path.getsize(path) > 0:
                break
            time.sleep(0.1)
        return read_ppm(path)

    def close(self):
        self.proc.kill()
        self.proc.wait()


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    parts, pos = [], 0
    while len(parts) < 4:               # P6 width height maxval
        while data[pos:pos + 1].isspace():
            pos += 1
        start = pos
        while not data[pos:pos + 1].isspace():
            pos += 1
        parts.append(data[start:pos])
    w, h = int(parts[1]), int(parts[2])
    return w, h, data[pos + 1:]


def load_font():
    """Glyph bitmaps from src/gfx/font8x16.c, keyed by row bytes."""
    import re
    src = open(os.path.join(HERE, "..", "src", "gfx", "font8x16.c")).read()
    rows = re.findall(r"\{ (0x[0-9a-f]{2}(?:, 0x[0-9a-f]{2}){15}) \}", src)
    table = {}
    for code, r in enumerate(rows):
        key = bytes(int(b, 16) for b in r.split(", "))
        table.setdefault(key, bytes([code]).decode("cp437"))
    return table


FONT = None


def screen_text(img, cw=8, ch=16):
    """Reads the console back from a screendump: one string per text row.
    Unknown cells become '?'."""
    global FONT
    if FONT is None:
        FONT = load_font()
    w, h, px = img
    lines = []
    for row in range(h // ch):
        line = []
        for col in range(w // cw):
            x0, y0 = col * cw, row * ch
            cell = [[pixel(img, x0 + x, y0 + y) for x in range(cw)] for y in range(ch)]
            bg = cell[0][0]
            for y in (ch - 1, ch - 2):          # glyphs rarely touch the corners
                if cell[y][0] != bg and cell[y][cw - 1] == cell[0][cw - 1]:
                    bg = cell[0][cw - 1]
            bits = bytes(sum(0x80 >> x for x in range(cw) if cell[y][x] != bg)
                         for y in range(ch))
            if not any(bits):
                line.append(" ")
            else:
                line.append(FONT.get(bits, "?"))
        lines.append("".join(line).rstrip())
    return lines


def pixel(img, x, y):
    w, _, px = img
    i = (y * w + x) * 3
    return tuple(px[i:i + 3])


# ---------------------------------------------------------------- tests

def test_boot_banner(b, opts):
    q = Qemu(b("kernel.img"))
    try:
        out = q.boot()
        for s in (b"bm33\x1b[0m kernel", b"board 920092", b"screen 640x360",
                  b"double buffer on"):
            assert s in out, f"missing {s!r} in boot log"
        hz = int(re.search(rb"measured (\d+) Hz", out).group(1))
        assert 900 <= hz <= 1100, f"timer IRQ rate {hz} Hz"  # QEMU host jitter
        plain = re.sub(rb"\x1b\[[0-9;]*m", b"", out).decode(errors="replace")
        for s in ("MMU+caches on", "console 80x21", "benchmark (us)",
                  "libc selftest: ok", "printf 3.142, sqrt(2) 1.414213562",
                  "IRQ on: timer 1000 Hz", "s32: playing the built-in demo.cart", "vsync probe"):
            assert s in plain, f"missing {s!r} in boot log"
        text = "\n".join(screen_text(q.screendump()))
        for s in ("Lua 5.4 on bm33", "2^10=1024.0 7//2=3 sqrt(2)=1.414214 THE QUICK BROWN FOX co:1,4,9",
                  "pcall caught: boot.lua:", "Lua bench: fib(25)=75025", "Lua memory:",
                  "type 'h' for help"):
            assert s in text, f"missing {s!r} on screen:\n{text}"
        assert all(len(l) < 80 for l in text.splitlines()), "boot output wraps:\n" + text
        q.send("i")
        out = q.expect("uptime")
        assert b"MMU/caches     : on" in out, out
        q.send("m")
        q.expect("KiB in use")
    finally:
        q.close()


def test_console_ansi_and_status(b, opts):
    q = Qemu(b("kernel.img"))
    try:
        q.boot()
        for _ in range(10):                 # the uptime appears once the monitor waits
            img = q.screendump()
            text = screen_text(img)
            if "up 00:00:" in text[0]:
                break
            time.sleep(0.2)
        assert text[0].startswith(" bm33 "), f"status bar: {text[0]!r}"
        assert "up 00:00:0" in text[0], f"uptime in status bar: {text[0]!r}"
        assert pixel(img, 2, 2) == (0, 170, 170), "status bar colour"
        # "bm33" in the banner is bright cyan (ESC[1;36m)
        sel = next(i for i, l in enumerate(text) if l.startswith("Lua 5.4"))
        colours = {pixel(img, x, sel * 16 + y) for x in range(0, 56) for y in range(16)}
        assert (255, 255, 85) in colours, "ANSI bright yellow not rendered"
        # scrolling: 40 unknown-command lines push the banner off screen
        for _ in range(20):
            q.send("x")
            q.expect("> ")
        text = screen_text(q.screendump())
        assert not any("Lua bench" in l for l in text), "console did not scroll"
        assert text[0].startswith(" bm33 "), "status bar scrolled away"
        assert any("unknown command (0x78)" in l for l in text)
    finally:
        q.close()


def test_screen_pattern(b, opts):
    q = Qemu(b("kernel.img"))
    try:
        q.boot()
        q.send("t")
        q.expect("press any key")
        w, h, px = img = q.screendump()
        assert (w, h) == (640, 360), (w, h)
        assert pixel(img, 0, 0) == (255, 255, 255), "border"
        assert pixel(img, 45, 100) == (191, 191, 191), "white bar"
        assert pixel(img, 500, 100) == (191, 0, 0), "red bar (RGB order)"
        assert pixel(img, 590, 100) == (0, 0, 191), "blue bar"
        digest = hashlib.sha256(px).hexdigest()
        ref = os.path.join(REF_DIR, "testpattern-640x360.sha256")
        os.makedirs(REF_DIR, exist_ok=True)
        if opts.update_ref or not os.path.exists(ref):
            with open(ref, "w") as f:
                f.write(digest + "\n")
            print(f"    wrote {ref}")
        else:
            with open(ref) as f:
                assert f.read().strip() == digest, "screen differs from reference"
        q.send(" ")                         # back to the console, text restored
        q.expect("> ")
        text = "\n".join(screen_text(q.screendump()))
        assert "Lua memory" in text and "test pattern shown" in text, text
    finally:
        q.close()


def _exception_case(b, key, needles, code):
    q = Qemu(b("kernel.img"))
    try:
        q.boot()
        q.send(key)
        out = q.expect(f"LED blink code: {code}")
        for s in needles:
            assert s.encode() in out, f"missing {s!r}:\n{out.decode(errors='replace')}"
        img = q.screendump()
        assert pixel(img, 639, 359) == (170, 0, 0), "panic screen not red"
        text = "\n".join(screen_text(img))
        assert "*** EXCEPTION:" in text and "System halted" in text, text
    finally:
        q.close()


def test_exc_undef(b, opts):
    _exception_case(b, "u", ["Undefined instruction", "PC=", "insn @PC = e7f000f0"], 1)


def test_exc_swi(b, opts):
    _exception_case(b, "s", ["Software interrupt", "insn @PC = ef000042"], 2)


def test_exc_prefetch_abort(b, opts):
    _exception_case(b, "b", ["Prefetch abort", "IFSR="], 3)


def test_exc_data_abort(b, opts):
    _exception_case(b, "a", ["Data abort", "DFAR=00008001"], 4)


def test_demo_60fps(b, opts):
    q = Qemu(b("kernel.img"))
    try:
        q.boot()
        q.send("d")
        time.sleep(2.0)
        # QEMU's display does not seem to honour the virtual offset, so a
        # screendump can catch a page mid-draw: take a few samples.
        for _ in range(5):
            img = q.screendump()
            top = screen_text(img)[0]
            if top.startswith(" bm33 M4 demo"):
                break
        assert top.startswith(" bm33 M4 demo") and "fps" in top, f"demo overlay: {top!r}"
        assert pixel(img, 320, 200) != (0, 0, 0), "demo background not drawn"
        time.sleep(4.0)
        q.send(" ")
        out = q.expect("dropped", timeout=10).decode(errors="replace")
        m = re.search(r"demo: (\d+) frames in ([\d.]+) s = ([\d.]+) fps \((\w+)\)\s+"
                      r"frame (\d+)-(\d+) us, (\d+) dropped", out)
        assert m, out
        frames, secs, fps, pacing = int(m[1]), float(m[2]), float(m[3]), m[4]
        dropped = int(m[7])
        assert 5.5 <= secs <= 8, secs
        assert 55 <= fps <= 62, f"{fps} fps"
        assert pacing == "timer", "QEMU has no real vsync"
        assert dropped <= frames // 20, f"{dropped} dropped frames"
        q.expect("> ")
        text = "\n".join(screen_text(q.screendump()))
        assert "demo:" in text and "dropped" in text, text
    finally:
        q.close()


def test_demo_monitor_key_stops(b, opts):
    q = Qemu(b("kernel.img"))
    try:
        q.boot()
        q.send("d")
        time.sleep(1.5)
        assert any(screen_text(q.screendump())[0].startswith(" bm33 M4 demo")
                   for _ in range(5)), "demo not on screen"
        q.send(" ")
        out = q.expect("dropped", timeout=5).decode(errors="replace")
        secs = float(re.search(r"frames in ([\d.]+) s", out).group(1))
        assert secs < 5, f"demo did not stop on key ({secs} s)"
    finally:
        q.close()


def test_lua_repl(b, opts):
    q = Qemu(b("kernel.img"))
    try:
        q.boot()
        q.send("l")
        q.expect("lua> ")
        q.send("print(2^10)\r")
        assert b"1024.0" in q.expect("lua> ")
        q.send("1 + 2, 'x' .. 'y'\r")              # expressions are printed
        assert b"3\txy" in q.expect("lua> ")
        q.send("t = {}\r")
        q.expect("lua> ")
        q.send("for i = 1, 3 do\r")                # continuation line
        q.expect(">> ")
        q.send("t[#t+1] = i * 10 end\r")
        q.expect("lua> ")
        q.send("table.concat(t, '-')\r")
        assert b"10-20-30" in q.expect("lua> ")
        q.send("error('boom')\r")                  # errors do not kill anything
        out = q.expect("lua> ")
        assert b"boom" in out and b"stack traceback" in out, out
        q.send("local x = nil + 1\r")
        assert b"attempt to perform arithmetic" in q.expect("lua> ")
        q.send("bm33.millis() > 0, math.type(bm33.micros())\r")
        assert b"true\tinteger" in q.expect("lua> ")
        q.send("string.format('%.3f %5.1f', math.pi, 2.25)\r")
        assert b"3.142   2.2" in q.expect("lua> ")
        q.send("select(2, bm33.mem()) > 0\r")
        assert b"true" in q.expect("lua> ")
        q.send("abc\x7f\x7f\x7f1+1\r")            # backspace editing
        assert b"2" in q.expect("lua> ")
        q.send("\x04")                            # Ctrl-D: back to the monitor
        q.expect("> ")
        q.send("h")
        q.expect("Lua REPL")
        text = "\n".join(screen_text(q.screendump()))
        assert "lua> print(2^10)" in text or "10-20-30" in text or "Lua REPL" in text, text
    finally:
        q.close()


def test_lua_out_of_memory(b, opts):
    q = Qemu(b("kernel.img"))
    try:
        q.boot()
        q.send("l")
        q.expect("lua> ")
        # grows past the 64 MiB Lua limit: must fail cleanly, not crash
        q.send("t = {} for i = 1, 1000 do t[i] = ('x'):rep(1 << 20) .. i end\r")
        out = q.expect("lua> ", timeout=60)
        assert b"not enough memory" in out, out
        q.send("t = nil collectgarbage() print('alive', 6 * 7)\r")
        assert b"alive\t42" in q.expect("lua> ", timeout=20)
    finally:
        q.close()


def yellow_square(img):
    pts = [(x, y) for y in range(0, 224) for x in range(0, 320) if pixel(img, x, y) == (230, 200, 40)]
    return (min(pts), max(pts)) if pts else None


def test_s32_boot_attract(b, opts):
    q = Qemu(b("kernel.img"))
    try:
        q.expect(DEMO, timeout=25)
        time.sleep(1.5)
        img, _ = settled_screen(q, lambda i, t: pixel(i, 5, 5) == (20, 30, 60))
        assert img[:2] == (320, 224), img[:2]
        assert pixel(img, 5, 5) == (20, 30, 60), "demo.cart background colour"
        sq1 = yellow_square(img)
        assert sq1 and sq1[1][0] - sq1[0][0] == 15, f"16x16 sprite: {sq1}"
        time.sleep(1.0)
        img2, _ = settled_screen(q, lambda i, t: pixel(i, 5, 5) == (20, 30, 60) and yellow_square(i))
        sq2 = yellow_square(img2)
        assert sq2 and sq2 != sq1, "attract mode should move the sprite"
        out = q.expect(B33_DEMO, timeout=30).decode(errors="replace")
        q.send("q")
        q.expect(PROMPT, timeout=30)
        m = re.search(r'"Demo - quadrato mobile" (\d+) ticks, ([\d.]+) fps \(attract\), (\d+) dropped', out)
        assert m, out
        assert 570 <= int(m[1]) <= 620 and 55 <= float(m[2]) <= 62, m.group(0)
        q.expect("> ")
        img = q.screendump()
        assert img[:2] == (640, 360), "console resolution restored"
        assert any("s32:" in l for l in screen_text(img)), "stats on the console"
    finally:
        q.close()


def test_s32_keys(b, opts):
    q = Qemu(b("kernel.img"))
    try:
        q.boot()
        q.send("g")
        time.sleep(1.0)
        start = yellow_square(q.screendump())
        assert start and start[0] == (150, 100), f"initial position {start}"
        for _ in range(25):                    # right: 'd' (held ~10 ticks each)
            q.send("d")
            time.sleep(0.05)
        time.sleep(0.5)
        right = yellow_square(q.screendump())
        assert right[0][0] > start[0][0] + 40 and right[0][1] == 100, f"{start} -> {right}"
        for _ in range(10):
            q.send("\x1b[A")                   # arrow up
            time.sleep(0.05)
        time.sleep(0.5)
        up = yellow_square(q.screendump())
        assert up[0][1] < 100, f"{right} -> {up}"
        q.send("q")
        out = q.expect("render", timeout=10).decode(errors="replace")
        assert "(attract)" not in out, out
        q.expect("> ")
    finally:
        q.close()


B33_COLOURS = [(248, 0, 0), (0, 252, 0), (0, 0, 248), (248, 252, 248)]


def settled_screen(q, ok, tries=8):
    """QEMU shows page 0 even while it is being drawn (it ignores the
    virtual offset), so a screendump can catch a frame half drawn: retry
    until `ok(img, text)` holds."""
    for _ in range(tries):
        img = q.screendump()
        text = screen_text(img)
        if ok(img, text):
            return img, text
    return img, text


def test_b33_boot_demo(b, opts):
    q = Qemu(b("kernel.img"))
    try:
        q.expect(DEMO, timeout=25)
        q.send("q")
        q.expect(B33_DEMO, timeout=20)
        out = q.expect("b33 bench:", timeout=20)
        time.sleep(3.0)
        img, text = settled_screen(q, lambda i, t: t[0].startswith("bm33 native") and "sprites" in t[-1])
        assert img[:2] == (640, 360), img[:2]
        assert text[0].startswith("bm33 native .b33") and "fps" in text[0], text[0]
        assert "sprites" in text[-1] and "attract" in text[-1], text[-1]
        got = [pixel(img, 640 - 80 + i * 20 + 8, 8) for i in range(4)]
        assert got == B33_COLOURS, f"RGB565 colour check {got}"
        out = q.expect(PROMPT, timeout=30).decode(errors="replace")
        m = re.search(r'b33: "bm33 native demo" (\d+) frames, ([\d.]+) fps', out)
        assert m and 850 <= int(m[1]) <= 920 and 55 <= float(m[2]) <= 62, out
        assert re.search(r"update\+draw avg [\d.]+ ms", out), out
        q.expect("> ")
        assert q.screendump()[:2] == (640, 360)
    finally:
        q.close()


def test_b33_keys(b, opts):
    q = Qemu(b("kernel.img"))
    try:
        q.boot()
        q.send("n")
        time.sleep(1.5)
        _, text = settled_screen(q, lambda i, t: "attract" in t[-1])
        assert "attract" in text[-1], text[-1]
        for _ in range(10):
            q.send("d")
            time.sleep(0.05)
        time.sleep(0.5)
        _, text = settled_screen(q, lambda i, t: "arrows/wasd" in t[-1])
        bottom = text[-1]
        assert "arrows/wasd" in bottom, bottom
        q.send("q")
        q.expect("update+draw", timeout=10)
        q.expect("> ")
    finally:
        q.close()


def _upload(q, data):
    q.send("U")
    q.expect("15 s timeout\r\n")
    loader = bm33_load.Loader(q.port, echo=q)
    return loader.upload(data)


def _b33(src):
    return mkb33.pack(src.encode(), title="test cart")


def test_b33_upload_errors(b, opts):
    q = Qemu(b("kernel.img"))
    try:
        q.boot()
        cases = [
            ("function _update() error('boom42') end", "boom42"),
            ("function _draw() while true do end end", "cart timeout"),
            ("function _init( end", "main.lua:1"),
            ("local x = nil + 1", "attempt to perform arithmetic"),
        ]
        for src, needle in cases:
            assert _upload(q, _b33(src)), src
            out = q.expect("> ", timeout=30).decode(errors="replace")
            assert "stopped with an error" in out and needle in out, out
            img = q.screendump()
            assert img[:2] == (640, 360), "console restored after the error"
        # sandbox: no file loading
        assert _upload(q, _b33("function _init() assert(dofile == nil and load == nil and io == nil and os == nil) error('sandbox ok') end"))
        assert b"sandbox ok" in q.expect("> ", timeout=20)
        # a working cart that exits by itself is not needed: 'q' stops it
        assert _upload(q, _b33("function _draw() cls(0x102030) print('hello', 8, 16) end"))
        time.sleep(1.0)
        img, _ = settled_screen(q, lambda i, t: "hello" in t[1])
        assert pixel(img, 300, 300) == (16, 32, 48), pixel(img, 300, 300)
        assert "hello" in screen_text(img)[1], screen_text(img)[:3]
        q.send("q")
        q.expect("update+draw", timeout=10)
        q.expect("> ")
    finally:
        q.close()


def test_upload_s32_and_corrupt(b, opts):
    q = Qemu(b("kernel.img"))
    try:
        q.boot()
        with open(os.path.join(HERE, "..", "spec", "s32", "conformance", "demo.cart"), "rb") as f:
            cart = f.read()
        assert _upload(q, cart)
        time.sleep(1.0)
        assert q.screendump()[:2] == (320, 224), "s32 cart via upload"
        q.send("q")
        q.expect("render", timeout=10)
        q.expect("> ")
        # CRC failure is reported, the monitor carries on
        q.send("U")
        q.expect("15 s timeout\r\n")
        import struct, zlib
        data = b"BM33CART" + b"x" * 100
        q.send(b"BM33" + struct.pack("<II", len(data), zlib.crc32(data) ^ 1))
        loader = bm33_load.Loader(q.port, echo=q)
        assert loader._reply() == b"OK"
        q.send(data)
        assert loader._reply() == b"CE"
        q.send("h")
        q.expect("commands:")
    finally:
        q.close()


def test_stress_monitor(b, opts):
    q = Qemu(b("kernel.img"))
    try:
        q.boot()
        q.send("S")
        q.expect("stress test:", timeout=10)
        time.sleep(3)
        _, text = settled_screen(q, lambda i, t: t[0].startswith("stress:"))
        assert text[0].startswith("stress:"), text[0]
        out = q.expect("3D spheres 96 (Lua)", timeout=300)
        out += q.expect("> ", timeout=60)
        plain = re.sub(rb"\x1b\[[0-9;]*m", b"", out).decode(errors="replace")
        for name in ("sprites 16x16 (C)", "sprites 32x32 (C)", "triangles 2D ~170px",
                     "3D spheres 96 (C)", "sprites 16x16 (Lua)"):
            m = re.search(re.escape(name) + r"\s+(\S+)", plain)
            assert m, f"{name} missing:\n{plain}"
        m = re.search(r"sprites 16x16 \(C\)\s+(\d+)\s+(\d+)", plain)
        assert m and int(m[2]) > int(m[1]) > 100, m and m.group(0)
        assert re.search(r"3D spheres 96 \(C\)\s+\d+ \(\d+ tri\)", plain), plain
        text = "\n".join(screen_text(q.screendump()))
        assert "sprites 16x16 (C)" in text and "3D spheres 96 (Lua)" in text, text
    finally:
        q.close()


def test_chainloader(b, opts):
    q = Qemu(b("chainloader.img"))
    try:
        loader = bm33_load.Loader(q.port, echo=q)
        assert loader.wait_ready(timeout=10), "chainloader did not announce itself"
        assert b"bm33 chainloader" in q.buf
        assert loader.upload_file(b("kernel.img")), "upload failed"
        q.boot()

        # Reboot from the monitor: chainloader comes back and takes a new kernel.
        q.send("r")
        q.expect("rebooting")
        assert loader.wait_ready(timeout=10), "no chainloader after reboot"
        assert loader.upload_file(b("kernel.img")), "second upload failed"
        q.boot()
    finally:
        q.close()


def test_chainloader_bad_crc(b, opts):
    q = Qemu(b("chainloader.img"))
    try:
        loader = bm33_load.Loader(q.port, echo=q)
        assert loader.wait_ready(timeout=10)
        import struct
        q.send(b"BM33" + struct.pack("<II", 4, 0xDEADBEEF))
        assert loader._reply() == b"OK"
        q.send(b"\x00\x00\x00\x00")
        assert loader._reply() == b"CE"
        assert loader.wait_ready(timeout=5), "loader did not recover"
    finally:
        q.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", default="build")
    ap.add_argument("--update-ref", action="store_true")
    ap.add_argument("-k", dest="filter", default="")
    opts = ap.parse_args()

    def b(name):
        return os.path.join(opts.build, name)

    tests = [(n, f) for n, f in globals().items()
             if n.startswith("test_") and opts.filter in n]
    failed = 0
    for name, fn in tests:
        t0 = time.time()
        try:
            fn(b, opts)
            print(f"PASS {name} ({time.time() - t0:.1f}s)")
        except Exception:
            failed += 1
            print(f"FAIL {name}")
            traceback.print_exc()
    print(f"\n{len(tests) - failed}/{len(tests)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
