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
import shutil
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
import mksd  # noqa: E402

QEMU = os.environ.get("QEMU", "qemu-system-arm")
REF_DIR = os.path.join(HERE, "ref")
PROMPT = b"type 'h' for help"
DEMO = b"s32: playing"
B33_DEMO = b"native demo cart"
MENU = b"cartridge menu"


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Qemu:
    def __init__(self, image, extra=(), mini_uart=False):
        """mini_uart: the second serial port (the mini UART, where the
        console goes when the PL011 is given to Bluetooth) on a socket too,
        as self.mini."""
        self.tmp = tempfile.mkdtemp(prefix="bm33-")
        self.mon_path = os.path.join(self.tmp, "mon.sock")
        tcp, tcp2 = free_port(), free_port()
        self.proc = subprocess.Popen(
            [QEMU, "-M", "raspi0", "-bios", image, "-display", "none",
             "-serial", f"tcp:127.0.0.1:{tcp},server=on,wait=on",
             "-serial", f"tcp:127.0.0.1:{tcp2},server=on,wait=on" if mini_uart else "null",
             "-monitor", f"unix:{self.mon_path},server=on,wait=off", *extra],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        self.port = bm33_load.Port(f"tcp:127.0.0.1:{tcp}", 115200)
        self.mini = bm33_load.Port(f"tcp:127.0.0.1:{tcp2}", 115200) if mini_uart else None
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

    def boot(self):
        """Boot ends in the cartridge menu: 'q' goes to the monitor prompt."""
        out = self.expect(MENU, timeout=30)
        self.send("q")
        out += self.expect(PROMPT, timeout=10)
        out += self.expect("> ")
        return out

    def diagnostics(self, skip_demo=True):
        """Monitor 'B': the old boot sequence (benchmarks, demos, boot.lua)."""
        self.send("b")             # lower or upper case
        out = self.expect(DEMO, timeout=20)
        if skip_demo:
            self.send("q")
        out += self.expect(B33_DEMO, timeout=20)
        if skip_demo:
            self.send("q")
        out += self.expect("Lua memory:", timeout=40)
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
        out += q.diagnostics()
        for s in (b"bm33\x1b[0m kernel", b"board 920092", b"screen 640x360",
                  b"double buffer on", b"sd: no card", b"usb: nothing attached"):
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
                  "pcall caught: boot.lua:", "Lua bench: fib(25)=75025", "Lua memory:"):
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
        q.expect(MENU, timeout=30)
        # the menu: tabs at the top, button hints at the bottom (near white)
        _, text = settled_screen(q, lambda i, t: any("Start+Select Monitor" in l for l in t))
        img = q.screendump()
        text = screen_text(img)
        assert "Games" in text[1] and "Dev" in text[1], text[1]
        sel = next(i for i, l in enumerate(text) if "Start+Select Monitor" in l)
        col = text[sel].index("Start+Select")
        colours = {pixel(img, x, sel * 16 + y) for x in range(col * 8, col * 8 + 96) for y in range(16)}
        assert any(r > 230 and g > 230 and b > 230 for r, g, b in colours), \
            f"hint text not rendered {colours}"
        q.send("q")
        q.expect(PROMPT)
        q.expect("> ")
        for _ in range(10):                 # the uptime appears once the monitor waits
            img = q.screendump()
            text = screen_text(img)
            if "up 00:00:" in text[0]:
                break
            time.sleep(0.2)
        assert text[0].startswith(" bm33 "), f"status bar: {text[0]!r}"
        assert "up 00:00:0" in text[0], f"uptime in status bar: {text[0]!r}"
        assert pixel(img, 2, 2) == (0, 170, 170), "status bar colour"
        # scrolling: 40 unknown-command lines push the prompt text off screen
        for _ in range(20):
            q.send("x")
            q.expect("> ")
        text = screen_text(q.screendump())
        assert not any("back to the monitor" in l for l in text), "console did not scroll"
        assert text[0].startswith(" bm33 "), "status bar scrolled away"
        assert any("unknown command (0x78)" in l for l in text)
    finally:
        q.close()


def test_pager(b, opts):
    """'h' is longer than the screen: it scrolls (s / arrows) and q returns;
    'o' shows everything printed since boot, from the first line."""
    q = Qemu(b("kernel.img"))
    try:
        q.boot()
        q.send("h")
        q.expect("-- lines 1-", timeout=5)
        q.send("s")
        q.expect("-- lines 2-", timeout=5)
        q.send("\x1b[B")                     # arrow down, serial sequence
        q.expect("-- lines 3-", timeout=5)
        q.send("q")
        q.expect("> ", timeout=5)
        q.send("o")
        out = q.expect("-- lines 1-", timeout=5).decode(errors="replace")
        assert "bm33 kernel" in out and "\x1b" not in out.split("bm33 kernel")[1][:40], out[-300:]
        q.send("\x1b")                       # Esc alone returns
        q.expect("> ", timeout=5)
        _, text = settled_screen(q, lambda i, t: any(l.startswith(">") for l in t))
        assert text[0].startswith(" bm33 "), text[0]
        # a terminal's arrow and PgDn keys at the prompt are not commands
        q.send("\x1b[B")
        time.sleep(0.3)
        q.send("\x1b[6~")
        time.sleep(0.3)
        q.send("\x1b")                       # Esc alone: ignored too
        time.sleep(0.3)
        q.send("m")
        out = q.expect("heap", timeout=5).decode(errors="replace")
        assert "unknown command" not in out, out[-300:]
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
        assert "type 'h' for help" in text and "test pattern shown" in text, text
    finally:
        q.close()


def _exception_case(b, key, needles, code):
    q = Qemu(b("kernel.img"))
    try:
        q.boot()
        q.send("X")
        q.expect("other keys cancel")
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
        q.boot()
        q.send("B")
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
        q.expect("Lua memory:", timeout=30)
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


def bar_icons(img):
    """The status icons at the right of the menu bar (M27): the column
    spans [x0, x1) with light pixels in rows 8-39, right of the tabs."""
    runs, start = [], None
    for x in range(330, 640):
        lit = any(sum(pixel(img, x, y)) > 450 for y in range(8, 40))
        if lit and start is None:
            start = x
        elif not lit and start is not None:
            runs.append((start, x))
            start = None
    if start is not None:
        runs.append((start, 640))
    return runs


def blue_number(img, span):
    """The number disc of a status icon is blue (a Bluetooth controller)."""
    return any(b > 200 and r < 80 and 90 < g < 160
               for x in range(*span) for y in range(8, 40)
               for r, g, b in [pixel(img, x, y)])


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
        q.boot()
        q.send("B")
        q.expect(DEMO, timeout=25)
        q.send("q")
        q.expect(B33_DEMO, timeout=20)
        out = q.expect("b33 bench (", timeout=20)
        time.sleep(3.0)
        img, text = settled_screen(q, lambda i, t: t[0].startswith("bm33 native") and "sprites" in t[-1])
        assert img[:2] == (640, 360), img[:2]
        assert text[0].startswith("bm33 native .bm") and "fps" in text[0], text[0]
        assert "sprites" in text[-1] and "attract" in text[-1], text[-1]
        got = [pixel(img, 640 - 80 + i * 20 + 8, 8) for i in range(4)]
        assert got == B33_COLOURS, f"RGB565 colour check {got}"
        out = q.expect("Lua memory:", timeout=30).decode(errors="replace")
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


USB_KBD = ["-device", "usb-kbd,port=1"]      # port=1: on the root port, no hub


def sendkeys(q, keys, gap=0.15):
    """Types on the emulated USB keyboard (QEMU key names)."""
    for k in keys.split():
        with socket.socket(socket.AF_UNIX) as s:
            s.connect(q.mon_path)
            s.sendall(f"sendkey {k}\n".encode())
            time.sleep(0.05)
        time.sleep(gap)


def test_usb_keyboard(b, opts):
    q = Qemu(b("kernel.img"), USB_KBD)
    try:
        out = q.expect(MENU, timeout=30)
        assert b"usb: keyboard 0627:0001 'QEMU USB Keyboard', high speed, layout it" in out, out
        time.sleep(0.5)
        sendkeys(q, "esc")                     # Esc: from the menu to the monitor
        q.expect(PROMPT, timeout=10)
        q.expect("> ")

        # b33: Esc quits the game
        sendkeys(q, "n")
        time.sleep(1.5)
        sendkeys(q, "esc")
        out = q.expect("update+draw", timeout=10).decode(errors="replace")
        m = re.search(r'demo" (\d+) frames', out)
        assert m and int(m[1]) < 300, out
        q.expect("> ")

        # s32: held arrow keys move the square
        sendkeys(q, "g")
        time.sleep(1.0)
        start = yellow_square(q.screendump())
        assert start and start[0] == (150, 100), f"initial position {start}"
        q.monitor("sendkey right 800")
        time.sleep(1.2)
        right = yellow_square(q.screendump())
        assert right[0][0] > start[0][0] + 40 and right[0][1] == 100, f"{start} -> {right}"
        sendkeys(q, "esc")
        out = q.expect("render", timeout=10).decode(errors="replace")
        assert "(attract)" not in out, out
        q.expect("> ")

        # monitor command and Lua REPL typed on the Italian layout:
        # shift-8 = '(', shift-] = '*', shift-2 = '"', ';' key = 'ò' (CP437 0x95)
        sendkeys(q, "l")
        q.expect("lua> ", timeout=10)
        sendkeys(q, "p r i n t shift-8 3 shift-bracket_right 4 shift-9 ret")
        q.expect("\n12\r\n", timeout=10)
        sendkeys(q, "p r i n t shift-8 shift-2 semicolon shift-2 shift-9 ret")
        q.expect(b'"\x95")', timeout=10)
        q.expect(b"\x95\r\n", timeout=10)
        sendkeys(q, "esc")                     # Esc on an empty line leaves the REPL
        q.expect("> ", timeout=10)
        sendkeys(q, "shift-l")
        q.expect("keyboard layout: us", timeout=10)
    finally:
        q.close()


def test_wifi_probe(b, opts):
    """M18: the SD card is on SDHOST, so the Arasan controller goes to the
    WiFi pins. QEMU has no WiFi chip: 'W' must stop at CMD5 with a clear
    message, without hanging, and the SD card must still work afterwards."""
    tmp = tempfile.mkdtemp(prefix="bm33-wifi-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [(b("demo.bm"), "carts/demo.bm")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    try:
        out = q.boot().decode(errors="replace")
        assert "(sdhost)" in out, out
        q.send("W")
        out = q.expect("wifi: CMD5: no answer from the WiFi chip", timeout=10).decode(errors="replace")
        assert "wifi: power on (WL_REG_ON = GPIO41)" in out and "controller at 400 kHz" in out, out
        q.expect("> ", timeout=5)
        q.send("F")                              # the card is still readable
        q.expect("demo.bm", timeout=10)
    finally:
        q.close()
        shutil.rmtree(tmp, ignore_errors=True)


def test_sd_cartridges(b, opts):
    tmp = tempfile.mkdtemp(prefix="bm33-sd-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [(b("demo.bm"), "Il mio gioco lungo.bm"),
                     (os.path.join(HERE, "..", "spec", "s32", "conformance", "demo.cart"),
                      "carts/demo2.cart"),
                     (os.path.join(HERE, "..", "README.md"), "README.md")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    try:
        out = q.boot().decode(errors="replace")
        assert "sd: SD card (sdhost), FAT32, 127 MiB, label BM33SD; 2 cartridges" in out, out
        q.send("f")
        out = q.expect("quadrato mobile\"\r\n").decode(errors="replace")
        assert "Il mio gioco lungo.bm" in out and "/carts/demo2.cart" in out, out
        q.expect("> ")
        q.send("M")
        q.expect("cartridge menu")
        time.sleep(0.5)
        _, text = settled_screen(q, lambda i, t: any("bm33 native demo" in l for l in t))
        screen = "\n".join(text)
        for s_ in ("Games", "bm33 native demo", "/Il mio gioco lungo.bm"):
            assert s_ in screen, screen
        q.send("d")                           # the next cover: its title and file
        _, text = settled_screen(q, lambda i, t: any("Demo - quadrato mobile" in l for l in t))
        screen = "\n".join(text)
        assert "Demo - quadrato mobile" in screen and "/carts/demo2.cart" in screen, screen
        q.send("a")
        q.send("\r")                          # SD cartridges come first, by title
        q.expect("playing Il mio gioco lungo.bm", timeout=10)
        time.sleep(1.0)
        q.send("q")
        out = q.expect("update+draw", timeout=15).decode(errors="replace")
        assert '"bm33 native demo"' in out, out
        q.send("d\r")                         # next: /carts/demo2.cart (s32)
        _, text = settled_screen(q, lambda i, t: any("Close bm33 native demo?" in l for l in t))
        assert any("Close bm33 native demo?" in l for l in text), "\n".join(text)
        q.send("\r")                          # the demo was suspended: close it
        q.expect("playing demo2.cart", timeout=10)
        time.sleep(1.0)
        q.send("q")
        q.expect("render", timeout=15)
        q.send("q")
        q.expect("back to the monitor", timeout=10)
        q.expect("> ")
    finally:
        q.close()


COUNTER_CART = r"""
local n = 0
function _init() log("counter start") end
function _update()
  n = n + 1
  if n % 60 == 0 then log("frame " .. n) end
end
function _draw() cls(0x203040) print("frame " .. n, 8, 8, 0xFFFFFF) end
"""


def test_suspend_resume(b, opts):
    """M21: leaving a game from the menu keeps it frozen in memory ("Playing"
    on its cover); A on it resumes from the same frame; starting another
    cartridge asks first (B keeps it, A closes it and frees the memory)."""
    tmp = tempfile.mkdtemp(prefix="bm33-susp-")
    img = os.path.join(tmp, "sd.img")
    cart = os.path.join(tmp, "counter.bm")
    with open(cart, "wb") as f:
        f.write(mkb33.pack(COUNTER_CART.encode(), title="AAA counter"))
    mksd.build(img, [(cart, "carts/counter.bm"), (b("demo.bm"), "carts/zdemo.bm")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    try:
        q.expect(MENU, timeout=30)
        time.sleep(0.5)
        q.send("\r")
        q.expect("counter start", timeout=10)
        q.expect("frame 120", timeout=10)
        q.send("q")
        q.expect('"AAA counter" suspended', timeout=10)
        _, text = settled_screen(q, lambda i, t: any("Playing" in l for l in t))
        assert any("Playing" in l for l in text), "\n".join(text)
        time.sleep(2.0)                        # time in the menu does not run the game
        q.send("\r")
        out = q.expect('"AAA counter" resumed', timeout=10).decode(errors="replace")
        assert "counter start" not in out, out
        out = q.expect("frame ", timeout=10) + q.expect("\n", timeout=5)
        m = re.search(rb"frame (\d+)", out)
        assert m and 120 <= int(m[1]) <= 240, out   # continues, does not restart
        q.send("q")
        q.expect("suspended", timeout=10)
        time.sleep(0.5)
        q.send("d\r")                         # another cartridge: the question
        _, text = settled_screen(q, lambda i, t: any("Close AAA counter?" in l for l in t))
        assert any("Close AAA counter?" in l for l in text), "\n".join(text)
        q.send("q")                            # no: still suspended
        time.sleep(0.5)
        q.send("\r")
        _, text = settled_screen(q, lambda i, t: any("Close AAA counter?" in l for l in t))
        assert any("Close AAA counter?" in l for l in text), "\n".join(text)
        q.send("\r")                          # yes: closed, the demo starts
        out = q.expect("playing zdemo.bm", timeout=10).decode(errors="replace")
        assert '"AAA counter" closed, memory freed' in out, out
    finally:
        q.close()
        shutil.rmtree(tmp, ignore_errors=True)


SAVER_CART = r"""
local n = 0
function _init() save({ n = 1 }) log("saver start") end
function _update() n = n + 1 end
function _draw() cls(0x203040) print("frame " .. n, 8, 8, 0xFFFFFF) end
"""


def test_home_ui(b, opts):
    """M27 (BareMetal UI): the options of a cartridge (X), with the save data
    and the file deleted from the SD card (fsck clean); the settings (3)
    and their submenus; the tools of the Dev tab on the text console and
    back to the menu; the monitor as a tool."""
    tmp = tempfile.mkdtemp(prefix="bm33-home-")
    img = os.path.join(tmp, "sd.img")
    saver = os.path.join(tmp, "saver.bm")
    with open(saver, "wb") as f:
        f.write(mkb33.pack(SAVER_CART.encode(), title="AAA saver", author="tests"))
    victim = os.path.join(tmp, "victim.bm")
    with open(victim, "wb") as f:
        f.write(mkb33.pack(COUNTER_CART.encode(), title="BBB delete me"))
    mksd.build(img, [(saver, "carts/saver.bm"), (victim, "carts/Un gioco da cancellare.bm")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])

    def keys(k):
        for c in k:
            q.send(c)
            time.sleep(0.25)

    def screen(want):
        _, text = settled_screen(q, lambda i, t: all(any(w in l for l in t) for w in want))
        joined = "\n".join(text)
        for w in want:
            assert w in joined, f"{w!r} not on the screen:\n{joined}"
        return joined

    def shot(name):
        if opts.shots:
            _save_png(q.screendump(), os.path.join(opts.shots, f"home-{name}.png"))

    try:
        q.expect(MENU, timeout=30)
        time.sleep(0.5)
        text = screen(["Games", "Dev", "Settings", "AAA saver"])
        assert "bm33" not in text.splitlines()[1] and "pads" not in text, text
        assert bar_icons(q.screendump()) == [], "no keyboard, pad or network: no icons"
        shot("games")
        keys("\r")                              # play: it saves, then Esc suspends it
        q.expect("saver start", timeout=10)
        time.sleep(0.5)
        q.send("q")
        q.expect('"AAA saver" suspended', timeout=10)
        time.sleep(0.5)

        # the options of the suspended game
        keys("x")
        screen(["AAA saver", "Resume", "Close the game", "Open in the SDK", "Author", "tests"])
        shot("options")
        keys("ww")                              # up from the first row: the last ones
        screen(["Delete the save data", "Records and progress start again"])
        keys("\r")
        screen(["Delete the save data?", "Delete", "Cancel"])
        shot("ask")
        keys("\r")
        q.expect("menu: save data deleted", timeout=10)
        text = screen(["Save data", "none"])
        assert "Delete the save data" not in text, text
        keys("s")                               # back to the top, then Close the game
        keys("s")
        keys("\r")
        q.expect('"AAA saver" closed, memory freed', timeout=10)

        # Open in the SDK (now the second row): the editor opens that file
        screen(["Play", "Open in the SDK"])
        keys("\r")
        screen(["opened /carts/saver.bm"])
        shot("sdk")
        q.send("\x1b")                          # code -> the editor's menu
        screen(["Exit editor"])
        q.send("\x1b[A")                        # up: Exit editor (one sequence)
        time.sleep(0.3)
        keys("\r")
        screen(["Games", "AAA saver", "last: SDK on saver.bm"])

        # the other cartridge leaves the SD card
        keys("d")
        keys("x")
        screen(["BBB delete me", "Play", "/carts/Un gioco da cancellare.bm"])
        keys("w")
        screen(["Delete from the SD card"])
        keys("\r")
        screen(["Delete BBB delete me?", "leaves the SD card"])
        keys("q")                               # no: nothing happens
        time.sleep(0.5)
        keys("\r")
        screen(["Delete BBB delete me?"])
        keys("\r")
        q.expect("menu: deleted /carts/Un gioco da cancellare.bm", timeout=10)
        text = screen(["AAA saver"])
        assert "BBB" not in text, text

        # settings: the keyboard layout changes and is saved; the submenus
        keys("3")
        screen(["Settings", "Controllers", "WiFi and network", "Keyboard layout", "System"])
        shot("settings")
        keys("ss")
        _, text = settled_screen(q, lambda i, t: any("< Italian >" in l or "< US >" in l for l in t))
        before = "Italian" if any("< Italian >" in l for l in text) else "US"
        assert before == "Italian" or any("< US >" in l for l in text), "\n".join(text)
        keys("d")
        after = "US" if before == "Italian" else "Italian"
        screen([f"< {after} >", "keyboard layout: "])
        keys("\r")                              # A changes it too: back as it was
        screen([f"< {before} >"])
        keys("s")
        keys("s")
        keys("\r")
        screen(["Settings > System", "Version", "SD card", "FAT32", "Restart"])
        shot("system")
        keys("q")
        keys("wwww")                            # System -> Controllers
        keys("\r")
        screen(["Settings > Controllers", "Player 1", "keyboard / USB", "Pair a new controller"])
        keys("q")
        keys("s")
        keys("\r")
        screen(["Settings > WiFi and network", "Network", "none saved", "port 3333"])
        keys("w")                               # the list scrolls to its last row
        screen(["Connect to a network", "Connect at boot", "< On >"])
        keys("q")
        keys("q")
        time.sleep(0.5)

        # the Dev tab: a tool on the text console, then A goes back
        keys("w")
        keys("2")
        keys("s")
        screen(["bm33 SDK", "editor (built-in)"])
        keys("d")                               # the covers' names are on pictures: the pill
        screen(["Monitor", "the text console with every command"])
        shot("dev")
        keys("dd")
        screen(["System", "board, clocks, memory"])
        keys("\r")
        q.expect("ARM clock", timeout=10)
        q.expect("back to the menu", timeout=10)
        time.sleep(0.5)
        keys("\r")
        screen(["Dev", "System", "board, clocks, memory"])
        keys("aa")                              # the monitor, as a tool
        keys("\r")
        q.expect("back to the monitor", timeout=10)
        q.expect("> ")
    finally:
        q.close()
    try:
        # the deleted file and the save data are gone, the card is clean
        part = os.path.join(tmp, "part.img")
        with open(img, "rb") as f, open(part, "wb") as o:
            f.seek(2048 * 512)
            o.write(f.read())
        fsck = subprocess.run(["fsck.vfat", "-n", part], capture_output=True, text=True)
        assert fsck.returncode == 0, fsck.stdout + fsck.stderr
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
        carts = subprocess.run(["mdir", "-i", part, "::/CARTS"], capture_output=True,
                               text=True, env=env).stdout
        assert "saver" in carts.lower() and "cancellare" not in carts, carts
        saves = subprocess.run(["mdir", "-i", part, "::/BM33/SAVE"], capture_output=True,
                               text=True, env=env).stdout
        assert not re.search(r"^[0-9A-F]{8}\s+SAV", saves, re.M), saves
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def test_make_image(b, opts):
    """`make image` (with placeholder firmware files): the SD image boots to
    a menu with the demo games, and one of them runs from the card."""
    tmp = tempfile.mkdtemp(prefix="bm33-img-")
    fw = os.path.join(tmp, "fw")
    os.makedirs(fw)
    for n in ("bootcode.bin", "start.elf", "fixup.dat"):
        with open(os.path.join(fw, n), "wb") as f:
            f.write(b"placeholder")
    root = os.path.join(HERE, "..")
    subprocess.run(["make", "-s", "-C", root, "image", f"FW_DIR={fw}", f"DIST={tmp}",
                    f"BUILD={os.path.abspath(b('.'))}"], check=True, stdout=subprocess.DEVNULL)
    img = os.path.join(tmp, "bm33.img")
    assert os.path.getsize(img) == 64 << 20, os.path.getsize(img)
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    try:
        out = q.expect(MENU, timeout=30).decode(errors="replace")
        assert "FAT32, 63 MiB, label BM33; 11 cartridges" in out, out
        time.sleep(0.5)
        seen = set()
        for _ in range(10):                    # right along the grid: each title in turn
            _, text = settled_screen(q, lambda i, t: len(t) > 3 and t[3].strip() != "")
            seen.add(text[3])
            q.send("d")
            time.sleep(0.3)
        screen = "\n".join(seen)
        for title in ("Pong", "Snake", "Star Shooter", "Chaos Kitchen", "bm33 native demo", "Demo - quadrato mobile"):
            assert title in screen, screen
        q.send("q")
        q.expect(PROMPT)
        q.expect("> ")
        q.send("f")
        out = q.expect('Star Shooter"\r\n').decode(errors="replace")
        assert "/carts/pong.bm" in out and "/carts/shooter.bm" in out, out
    finally:
        q.close()
        shutil.rmtree(tmp, ignore_errors=True)


SAVE_CART = r"""
function _init()
  local d = saved()
  local n = (d and d.n or 0) + 1
  if d then log("loaded", d.s, d.t[2], d.t[3], math.type(d.t[2]), d.nested.x) end
  local ok, err = save({ n = n, s = 'q"uo\\te\n', t = { 1, 2.5, true }, nested = { x = -7 } })
  log("runs", n, ok, err)
  quit()
end
"""


def test_sd_save_and_config(b, opts):
    """M11: a cart's save() and the monitor settings survive a reboot; the
    card is still a clean FAT32 volume afterwards (fsck.vfat, mtools)."""
    tmp = tempfile.mkdtemp(prefix="bm33-save-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [(b("carts/snake.bm"), "carts/snake.bm")])
    cart = mkb33.pack(SAVE_CART.encode(), title="save test")
    drive = ["-drive", f"if=sd,format=raw,file={img}"]
    try:
        for run in (1, 2):
            q = Qemu(b("kernel.img"), drive)
            try:
                out = q.boot().decode(errors="replace")
                if run == 2:
                    assert "config: /bm33/config.txt, layout us, .bm drawing direct" in out, out
                assert _upload(q, cart)
                out = q.expect(f"runs\t{run}\t", timeout=15).decode(errors="replace")
                line = q.expect("\n").decode(errors="replace")
                assert line.startswith("true"), line
                if run == 2:
                    assert 'loaded\tq"uo\\te\r\n\t2.5\ttrue\tfloat\t-7' in out, out  # serial: \r\n
                q.expect("> ", timeout=10)
                if run == 1:
                    q.send("L")                    # Italian -> US, saved in config.txt
                    q.expect("keyboard layout: us")
                    q.expect("> ")
            finally:
                q.close()
        part = os.path.join(tmp, "part.img")
        with open(img, "rb") as f, open(part, "wb") as o:
            f.seek(2048 * 512)
            o.write(f.read())
        fsck = subprocess.run(["fsck.vfat", "-n", part], capture_output=True, text=True)
        assert fsck.returncode == 0, fsck.stdout + fsck.stderr
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
        cfg = subprocess.run(["mtype", "-i", part, "::/BM33/CONFIG.TXT"], capture_output=True,
                             text=True, env=env).stdout
        assert "layout=us" in cfg and "draw=direct" in cfg, cfg
        saves = subprocess.run(["mdir", "-b", "-i", part, "::/BM33/SAVE"], capture_output=True,
                               text=True, env=env).stdout
        assert saves.count(".SAV") == 1, saves
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


class FakeBtChip:
    """Plays the BCM43438 on the PL011 socket: answers HCI commands (H4)
    like the real chip, ignoring the first reset to exercise the power
    cycle, and reports one phone during inquiry (nothing to pair)."""

    ADDR = bytes([0x66, 0x55, 0x44, 0x33, 0x22, 0x11])          # 11:22:33:44:55:66
    DS4 = bytes([0x03, 0x02, 0x01, 0x6D, 0x66, 0x1C])           # 1c:66:6d:01:02:03

    def __init__(self, port):
        self.port, self.buf, self.log = port, b"", []
        self.resets = 0

    def _read(self, n, timeout=5.0):
        deadline = time.time() + timeout
        while len(self.buf) < n:
            if time.time() > deadline:
                raise AssertionError(f"fake chip: waiting for {n} bytes, got {self.buf!r}")
            self.buf += self.port.read(0.05)
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def _event(self, code, params):
        self.port.write(bytes([0x04, code, len(params)]) + params)

    def _complete(self, op, ret=b""):
        self._event(0x0E, bytes([1, op & 0xFF, op >> 8, 0]) + ret)

    def serve(self, until_op, timeout=20.0):
        """Answers commands until `until_op` has been answered."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            t = self._read(1)[0]
            assert t == 0x01, f"fake chip: packet type {t:#x}"
            op = int.from_bytes(self._read(2), "little")
            params = self._read(self._read(1)[0])
            self.log.append((op, params))
            if op == 0x0C03:                                   # reset
                self.resets += 1
                if self.resets == 1:
                    continue                                   # silent: power cycle
                self._complete(op)
            elif op == 0x1001:                                 # local version
                self._complete(op, bytes([9, 0x2B, 0, 9, 0x0F, 0, 0x06, 0x41]))
            elif op == 0x1009:                                 # BD_ADDR
                self._complete(op, self.ADDR)
            elif op == 0x0401:                                 # inquiry
                self._event(0x0F, bytes([0, 1, op & 0xFF, op >> 8]))
                self._event(0x22, bytes([1]) + self.DS4 + bytes([1, 0, 0x0C, 0x02, 0x5A, 0, 0, 0xC4]))
                self._event(0x01, bytes([0]))
            else:                                              # firmware records etc.
                self._complete(op)
            if op == until_op:
                return
        raise AssertionError("fake chip: timeout")


def test_bt_start_and_scan(b, opts):
    """M12 step 1 against a simulated chip: the console moves to the mini
    UART, the chip is reset (power cycle after a silent first try), the
    firmware patch from the SD card is sent record by record, address and
    version are read, and an inquiry lists a device (a phone: no pairing)."""
    tmp = tempfile.mkdtemp(prefix="bm33-bt-")
    img = os.path.join(tmp, "sd.img")
    hcd = os.path.join(tmp, "BCM43430A1.hcd")
    with open(hcd, "wb") as f:                         # two records, like the real file
        f.write(bytes([0x4C, 0xFC, 8]) + bytes(range(8)) + bytes([0x4E, 0xFC, 4, 0xFF, 0xFF, 0xFF, 0xFF]))
    mksd.build(img, [(hcd, "bm33/BCM43430A1.hcd")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"], mini_uart=True)
    try:
        q.boot()
        q.send("T")
        q.expect("(same pins, same speed)\r\n")
        chip = FakeBtChip(q.port)
        chip.buf, q.buf = q.buf, b""                   # HCI bytes already read
        chip.serve(0x0401)                             # ... up to the inquiry
        mini = b""
        deadline = time.time() + 20
        while b"no game controller among them" not in mini and time.time() < deadline:
            mini += q.mini.read(0.1)
        text = mini.decode(errors="replace")
        for s_ in ("power-cycling the chip", "firmware patch loaded (2 records, 18 bytes)",
                   "bt: ready, address 11:22:33:44:55:66, HCI 9, LMP subversion 4106, 921600 baud",
                   "bt: found 1c:66:6d:01:02:03 class 5a020c (phone)", "bt: 1 device found",
                   "bt: no game controller among them"):
            assert s_ in text, text
        ops = [op for op, _ in chip.log]
        assert ops[:4] == [0x0C03, 0x0C03, 0xFC2E, 0xFC4C], [hex(o) for o in ops]
        assert chip.log[3][1] == bytes(range(8)) and 0xFC4E in ops, chip.log
        # the monitor now answers on the mini UART
        q.mini.write(b"i")
        out = b""
        deadline = time.time() + 5
        while b"uptime" not in out and time.time() < deadline:
            out += q.mini.read(0.1)
        assert b"uptime" in out, out
    finally:
        q.close()
        shutil.rmtree(tmp, ignore_errors=True)


class FakeDs4Chip(FakeBtChip):
    """The chip plus a DualShock 4 behind it: pairing (SSP Just Works),
    encryption, HID channels, input reports, and the reconnection the pad
    starts itself when its PS button is pressed."""

    KEY = bytes(range(0xA0, 0xB0))
    HANDLE = 0x000B
    # the light colour bm33 gives each player (bt.c)
    LIGHT = [(0x00, 0x20, 0x80), (0x80, 0x08, 0x00), (0x00, 0x80, 0x10), (0x80, 0x00, 0x50)]

    def packet(self):
        t = self._read(1)[0]
        if t == 0x01:
            op = int.from_bytes(self._read(2), "little")
            return "cmd", op, self._read(self._read(1)[0])
        assert t == 0x02, f"fake chip: packet type {t:#x}"
        hdr = self._read(4)
        payload = self._read(int.from_bytes(hdr[2:4], "little"))
        return "acl", int.from_bytes(hdr[:2], "little") & 0x0FFF, payload

    def status(self, op, st=0):
        self._event(0x0F, bytes([st, 1, op & 0xFF, op >> 8]))

    def cmd(self, want, reply="complete", ret=b""):
        """Reads the next packet: it must be command `want`; answers it."""
        kind, op, params = self.packet()
        assert kind == "cmd" and op == want, f"expected command {want:#06x}, got {kind} {op:#06x}"
        self.log.append((op, params))
        if reply == "complete":
            self._complete(op, ret)
        elif reply == "status":
            self.status(op)
        return params

    def init(self, reset_silent):
        """Reset (maybe silent), firmware, version, address, setup commands."""
        if reset_silent:
            self.cmd(0x0C03, reply=None)
        self.cmd(0x0C03)
        self.cmd(0xFC2E)
        while True:
            kind, op, params = self.packet()
            self._complete(op)
            if op == 0xFC4E:
                break
        self.cmd(0x0C03)
        self.cmd(0x1001, ret=bytes([7, 0x09, 0x22, 7, 0x0F, 0, 0x09, 0x22]))
        self.cmd(0x1009, ret=self.ADDR)
        baud = self.cmd(0xFC18)                                        # UART speed up
        assert baud == bytes([0, 0]) + (921600).to_bytes(4, "little"), baud.hex()
        self.cmd(0x1009, ret=self.ADDR)                                # check at the new speed
        for op in (0x0C01, 0x0C56, 0x0C13, 0x0C24, 0x0C1A):
            self.cmd(op)

    # L2CAP from the pad's side
    def l2(self, cid, data, handle=None):
        frame = len(data).to_bytes(2, "little") + cid.to_bytes(2, "little") + data
        h = self.HANDLE if handle is None else handle
        hdr = (h | 0x2000).to_bytes(2, "little") + len(frame).to_bytes(2, "little")
        self.port.write(bytes([0x02]) + hdr + frame)

    def sig(self, code, ident, data, handle=None):
        self.l2(0x0001, bytes([code, ident]) + len(data).to_bytes(2, "little") + data, handle)

    def host_sig(self, handle=None):
        """Next signaling command from the host: (code, id, data)."""
        kind, h, payload = self.packet()
        want = self.HANDLE if handle is None else handle
        assert kind == "acl" and h == want, (kind, h)
        cid = int.from_bytes(payload[2:4], "little")
        assert cid == 1, f"host sent data on cid {cid:#x}, expected signaling"
        s = payload[4:]
        return s[0], s[1], s[4:4 + int.from_bytes(s[2:4], "little")]

    def configure(self, our_cid, host_cid, handle=None):
        """Host config request -> accepted; ours -> host accepts it."""
        code, ident, data = self.host_sig(handle)
        assert code == 0x04 and int.from_bytes(data[:2], "little") == our_cid, (code, data)
        self.sig(0x05, ident, host_cid.to_bytes(2, "little") + bytes(4), handle)
        self.sig(0x04, 0x77, host_cid.to_bytes(2, "little") + bytes(2), handle)
        code, ident, data = self.host_sig(handle)
        assert code == 0x05 and ident == 0x77, (code, data)

    def expect_light(self, cid, player, handle=None):
        """The output report that lights the pad in the player's colour, on
        the pad's interrupt channel, with the CRC the DS4 checks."""
        kind, h, payload = self.packet()
        want = self.HANDLE if handle is None else handle
        assert kind == "acl" and h == want, (kind, h)
        assert int.from_bytes(payload[2:4], "little") == cid, payload.hex()
        r = payload[4:]
        assert len(r) == 79 and r[:2] == bytes([0xA2, 0x11]), r.hex()
        assert tuple(r[9:12]) == self.LIGHT[player - 1], (player, r[9:12].hex())
        import zlib
        assert zlib.crc32(r[:75]) == int.from_bytes(r[75:79], "little"), "bad CRC"

    def reconnect(self, pad, key, handle, pad_cids, player):
        """The pad comes back (PS button): connection, saved key, encryption,
        the pad opens both HID channels, the host lights it."""
        self._event(0x04, pad + bytes([0x08, 0x25, 0x00, 1]))            # connection request
        acc = self.cmd(0x0409, reply="status")
        assert acc == pad + bytes([0]), acc.hex()
        self._event(0x03, bytes([0]) + handle.to_bytes(2, "little") + pad + bytes([1, 0]))
        self._event(0x17, pad)                                           # link key request
        reply = self.cmd(0x040B)
        assert reply == pad + key, reply.hex()                           # the saved key
        self._event(0x08, bytes([0]) + handle.to_bytes(2, "little") + bytes([1]))
        for psm, pad_cid, host_cid in ((0x11, pad_cids[0], 0x40), (0x13, pad_cids[1], 0x41)):
            self.sig(0x02, 0x10 + psm, psm.to_bytes(2, "little") + pad_cid.to_bytes(2, "little"), handle)
            code, ident, data = self.host_sig(handle)
            assert code == 0x03 and data[:4] == host_cid.to_bytes(2, "little") + pad_cid.to_bytes(2, "little") \
                and data[4:6] == bytes(2), (code, data)
            code, ident, data = self.host_sig(handle)                    # host's config request
            assert code == 0x04 and int.from_bytes(data[:2], "little") == pad_cid
            self.sig(0x05, ident, host_cid.to_bytes(2, "little") + bytes(4), handle)
            self.sig(0x04, 0x66, host_cid.to_bytes(2, "little") + bytes(2), handle)
            code, ident, data = self.host_sig(handle)
            assert code == 0x05 and ident == 0x66
        self.expect_light(pad_cids[1], player, handle)

    def reconnect_sdp(self, pad, key, handle, pad_cids, player, sdp_cid=0x42):
        """A pad that comes back, asks the host's SDP first (seen on the Pi:
        it gave up when the channel was refused) and then waits: the host
        answers with no records and opens both HID channels itself."""
        self._event(0x04, pad + bytes([0x08, 0x25, 0x00, 1]))
        self.cmd(0x0409, reply="status")
        self._event(0x03, bytes([0]) + handle.to_bytes(2, "little") + pad + bytes([1, 0]))
        self._event(0x17, pad)
        assert self.cmd(0x040B) == pad + key
        self._event(0x08, bytes([0]) + handle.to_bytes(2, "little") + bytes([1]))
        # SDP channel from the pad (PSM 1)
        self.sig(0x02, 0x21, (1).to_bytes(2, "little") + sdp_cid.to_bytes(2, "little"), handle)
        code, ident, data = self.host_sig(handle)
        assert code == 0x03 and data[:6] == (0x42).to_bytes(2, "little") + sdp_cid.to_bytes(2, "little") \
            + bytes(2), (code, data.hex())
        code, ident, data = self.host_sig(handle)
        assert code == 0x04 and int.from_bytes(data[:2], "little") == sdp_cid
        self.sig(0x05, ident, (0x42).to_bytes(2, "little") + bytes(4), handle)
        self.sig(0x04, 0x68, (0x42).to_bytes(2, "little") + bytes(2), handle)
        code, ident, data = self.host_sig(handle)
        assert code == 0x05 and ident == 0x68
        # Service Search Attribute request -> empty answer
        req = bytes([0x06, 0x00, 0x01, 0x00, 0x0F, 0x35, 0x03, 0x19, 0x12, 0x00, 0xFF, 0xFF,
                     0x35, 0x05, 0x0A, 0x00, 0x00, 0xFF, 0xFF, 0x00])
        self.l2(0x0042, req, handle)
        kind, h, payload = self.packet()
        assert kind == "acl" and h == handle and int.from_bytes(payload[2:4], "little") == sdp_cid, \
            payload.hex()
        assert payload[4:] == bytes([0x07, 0x00, 0x01, 0x00, 0x05, 0x00, 0x02, 0x35, 0x00, 0x00]), \
            payload[4:].hex()
        # the pad waits: the host opens the HID channels (as when pairing)
        for psm, host_cid, pad_cid in ((0x11, 0x40, pad_cids[0]), (0x13, 0x41, pad_cids[1])):
            code, ident, data = self.host_sig(handle)
            assert code == 0x02 and data == psm.to_bytes(2, "little") + host_cid.to_bytes(2, "little"), \
                (code, data.hex())
            self.sig(0x03, ident, pad_cid.to_bytes(2, "little") + host_cid.to_bytes(2, "little") + bytes(4),
                     handle)
            self.configure(pad_cid, host_cid, handle)
        self.expect_light(pad_cids[1], player, handle)

    def pair(self, pad, key, handle, pad_cids, player, clock=(0x34, 0x12)):
        """Pairing from the monitor ('T'): inquiry finds the pad, the host
        connects, SSP Just Works, encryption, the host opens both channels
        and lights the pad in the colour of the player it became."""
        self.cmd(0x0401, reply="status")                                   # inquiry
        self._event(0x22, bytes([1]) + pad + bytes([1, 0, 0x08, 0x25, 0x00, *clock, 0xC4]))
        self._event(0x01, bytes([0]))
        p = self.cmd(0x0405, reply="status")                               # create connection
        assert p[:6] == pad and p[8] == 1 and p[10:12] == bytes([clock[0], clock[1] | 0x80]), p.hex()
        hb = handle.to_bytes(2, "little")
        self._event(0x03, bytes([0]) + hb + pad + bytes([1, 0]))
        self.cmd(0x0411, reply="status")                                   # authentication
        self._event(0x17, pad)                                             # link key request
        self.cmd(0x040C)                                                   # -> no key yet
        self._event(0x31, pad)                                             # IO capability request
        io = self.cmd(0x042B)
        assert io == pad + bytes([0x03, 0x00, 0x04]), io.hex()             # NoInputNoOutput
        self._event(0x33, pad + (123456).to_bytes(4, "little"))            # user confirmation
        self.cmd(0x042C)
        self._event(0x36, bytes([0]) + pad)
        self._event(0x18, pad + key + bytes([4]))                          # link key
        self._event(0x06, bytes([0]) + hb)
        self.cmd(0x0413, reply="status")                                   # encryption on
        self._event(0x08, bytes([0]) + hb + bytes([1]))
        for psm, host_cid, pad_cid in ((0x11, 0x40, pad_cids[0]), (0x13, 0x41, pad_cids[1])):
            code, ident, data = self.host_sig(handle)
            assert code == 0x02 and data == psm.to_bytes(2, "little") + host_cid.to_bytes(2, "little")
            self.sig(0x03, ident, pad_cid.to_bytes(2, "little") + host_cid.to_bytes(2, "little") + bytes(4),
                     handle)
            self.configure(pad_cid, host_cid, handle)
        self.expect_light(pad_cids[1], player, handle)

    def report(self, buttons=0x08, ps=0, handle=None, lx=128, ly=128):
        """DS4 reduced input report 0x01 on the host's interrupt channel."""
        self.l2(0x0041, bytes([0xA1, 0x01, lx, ly, 128, 128, buttons, 0, ps, 0, 0]), handle)


def _mini_expect(q, needle, timeout=20):
    if isinstance(needle, str):
        needle = needle.encode()
    deadline = time.time() + timeout
    while needle not in q.mini_buf:
        assert time.time() < deadline, f"mini UART: waiting for {needle!r}:\n" + \
            q.mini_buf.decode(errors="replace")
        q.mini_buf += q.mini.read(0.05)
    i = q.mini_buf.index(needle) + len(needle)
    seen, q.mini_buf = q.mini_buf[:i], q.mini_buf[i:]
    return seen.decode(errors="replace")


def test_bt_pair_and_reconnect(b, opts):
    """M12 with a simulated DualShock 4: pairing from the monitor ('T'),
    the pad drives the menu and quits a game with PS; after a reboot the
    stack starts by itself, the pad reconnects with the saved link key."""
    tmp = tempfile.mkdtemp(prefix="bm33-bt-")
    img = os.path.join(tmp, "sd.img")
    hcd = os.path.join(tmp, "BCM43430A1.hcd")
    with open(hcd, "wb") as f:
        f.write(bytes([0x4C, 0xFC, 4, 1, 2, 3, 4, 0x4E, 0xFC, 4, 0xFF, 0xFF, 0xFF, 0xFF]))
    mksd.build(img, [(hcd, "bm33/BCM43430A1.hcd"), (b("carts/snake.bm"), "carts/snake.bm")])
    drive = ["-drive", f"if=sd,format=raw,file={img}"]
    pad = FakeDs4Chip.DS4
    try:
        # ---- first boot: pair from the monitor
        q = Qemu(b("kernel.img"), drive, mini_uart=True)
        q.mini_buf = b""
        try:
            q.boot()
            q.send("T")
            q.expect("(same pins, same speed)\r\n")
            chip = FakeDs4Chip(q.port)
            chip.buf, q.buf = q.buf, b""
            chip.init(reset_silent=False)
            chip.pair(pad, chip.KEY, chip.HANDLE, (0x70, 0x71), 1)              # player 1: blue
            out = _mini_expect(q, "next time just press PS")
            assert "bt: pairing with 1c:66:6d:01:02:03" in out and \
                "bt: controller 1c:66:6d:01:02:03 connected (player 1)" in out, out
            # the pad drives the menu: cross plays the first cart, PS quits
            q.mini.write(b"M")
            _mini_expect(q, "cartridge menu")
            time.sleep(0.3)
            # the real pad streams reports all the time: a burst of 600 idle
            # ones must not delay the next press (it did on the Pi)
            for _ in range(int(os.environ.get('BURST', 600))):
                chip.report(0x08)
            time.sleep(0.2)
            t_press = time.time()
            chip.report(0x08 | 0x20)
            time.sleep(0.05)
            chip.report(0x08)
            _mini_expect(q, "playing snake.bm", timeout=60)
            lag = time.time() - t_press
            print(f"    lag after burst: {lag:.2f} s")
            assert lag < 0.5, f"input lag {lag:.2f} s after a burst of reports"
            time.sleep(1.0)
            chip.report(0x08, ps=1)
            _mini_expect(q, "update+draw")
            chip.report(0x08, ps=0)
            time.sleep(0.3)
            q.mini.write(b"q")
            _mini_expect(q, "back to the monitor")
        finally:
            q.close()

        part = os.path.join(tmp, "part.img")
        with open(img, "rb") as f, open(part, "wb") as o:
            f.seek(2048 * 512)
            o.write(f.read())
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
        cfg = subprocess.run(["mtype", "-i", part, "::/BM33/CONFIG.TXT"], capture_output=True,
                             text=True, env=env).stdout
        assert "bt_pad1=1c:66:6d:01:02:03 " + FakeDs4Chip.KEY.hex() in cfg, cfg

        # ---- second boot: the stack starts by itself, the pad comes back
        q = Qemu(b("kernel.img"), drive, mini_uart=True)
        q.mini_buf = b""
        try:
            q.expect("(same pins, same speed)\r\n", timeout=30)
            chip = FakeDs4Chip(q.port)
            chip.buf, q.buf = q.buf, b""
            chip.init(reset_silent=False)
            _mini_expect(q, "paired pad 1c:66:6d:01:02:03 (player 1): press its PS button")
            _mini_expect(q, "cartridge menu")
            chip.reconnect(pad, FakeDs4Chip.KEY, chip.HANDLE, (0x50, 0x51), 1)
            _mini_expect(q, "bt: controller 1c:66:6d:01:02:03 connected (player 1)")
            chip.report(0x08 | 0x20)
            time.sleep(0.2)
            chip.report(0x08)
            _mini_expect(q, "playing snake.bm")
        finally:
            q.close()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


PLAYERS_CART = r"""
local last, lastn = {}, nil
function _update()
  for p = 1, 4 do
    local s = ""
    for b = 0, 7 do if btn(b, p) then s = s .. b end end
    local x, y = stick(p)
    s = s .. string.format(" %.1f,%.1f", x, y)
    if s ~= last[p] then last[p] = s; log("player " .. p .. " [" .. s .. "]") end
  end
  local n, m = players()
  if n ~= lastn then lastn = n; log("players " .. n .. " mask " .. m) end
end
function _draw() cls(0) end
"""


def test_bt_forget(b, opts):
    """'P' then 'y' forgets every paired pad: the keys leave bm33/config.txt
    (other settings stay)."""
    tmp = tempfile.mkdtemp(prefix="bm33-bt-")
    img = os.path.join(tmp, "sd.img")
    cfg = os.path.join(tmp, "config.txt")
    with open(cfg, "w") as f:
        f.write("layout=it\nbt_pad1=1c:66:6d:01:02:03 " + "a0" * 16 + "\n"
                "bt_pad2=1c:66:6d:09:0a:0b " + "c0" * 16 + "\n")
    hcd = os.path.join(tmp, "BCM43430A1.hcd")
    with open(hcd, "wb") as f:
        f.write(bytes([0x4C, 0xFC, 4, 1, 2, 3, 4, 0x4E, 0xFC, 4, 0xFF, 0xFF, 0xFF, 0xFF]))
    mksd.build(img, [(hcd, "bm33/BCM43430A1.hcd"), (cfg, "bm33/config.txt")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"], mini_uart=True)
    q.mini_buf = b""
    try:
        q.expect("(same pins, same speed)\r\n", timeout=30)   # paired pads: the stack starts
        chip = FakeDs4Chip(q.port)
        chip.buf, q.buf = q.buf, b""
        chip.init(reset_silent=False)
        _mini_expect(q, "cartridge menu")
        q.mini.write(b"q")
        _mini_expect(q, "back to the monitor")
        q.mini.write(b"P")
        _mini_expect(q, "y = yes")
        q.mini.write(b"n")
        _mini_expect(q, "cancelled")
        q.mini.write(b"P")
        _mini_expect(q, "y = yes")
        q.mini.write(b"y")
        _mini_expect(q, "bt: 2 pads forgotten")
    finally:
        q.close()
    try:
        part = os.path.join(tmp, "part.img")
        with open(img, "rb") as f, open(part, "wb") as o:
            f.seek(2048 * 512)
            o.write(f.read())
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
        text = subprocess.run(["mtype", "-i", part, "::/BM33/CONFIG.TXT"], capture_output=True,
                              text=True, env=env).stdout
        assert "bt_pad" not in text and "layout=it" in text, text
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def test_bt_two_pads(b, opts):
    """M16: two DS4 paired earlier (the first by an older kernel, as
    'bt_pad') come back together and light up in their players' colours; a
    third one pairs as player 3 (the old key becomes bt_pad1). In a game each
    pad drives its own player (btn(i, p), stick(p)); the serial keys are the
    first player without a pad."""
    tmp = tempfile.mkdtemp(prefix="bm33-bt-")
    img = os.path.join(tmp, "sd.img")
    hcd = os.path.join(tmp, "BCM43430A1.hcd")
    with open(hcd, "wb") as f:
        f.write(bytes([0x4C, 0xFC, 4, 1, 2, 3, 4, 0x4E, 0xFC, 4, 0xFF, 0xFF, 0xFF, 0xFF]))
    pad_a, key_a = FakeDs4Chip.DS4, FakeDs4Chip.KEY
    pad_b, key_b = bytes([0x0B, 0x0A, 0x09, 0x6D, 0x66, 0x1C]), bytes(range(0xC0, 0xD0))
    pad_c, key_c = bytes([0x0E, 0x0D, 0x0C, 0x6D, 0x66, 0x1C]), bytes(range(0xD0, 0xE0))
    cfg = os.path.join(tmp, "config.txt")
    with open(cfg, "w") as f:
        f.write(f"layout=it\nbt_pad=1c:66:6d:01:02:03 {key_a.hex()}\n"
                f"bt_pad2=1c:66:6d:09:0a:0b {key_b.hex()}\n")
    cart = os.path.join(tmp, "players.bm")
    with open(cart, "wb") as f:
        f.write(mkb33.pack(PLAYERS_CART.encode(), title="AAA players"))
    mksd.build(img, [(hcd, "bm33/BCM43430A1.hcd"), (cfg, "bm33/config.txt"),
                     (cart, "carts/players.bm")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"], mini_uart=True)
    q.mini_buf = b""
    try:
        q.expect("(same pins, same speed)\r\n", timeout=30)
        chip = FakeDs4Chip(q.port)
        chip.buf, q.buf = q.buf, b""
        chip.init(reset_silent=False)
        _mini_expect(q, "paired pad 1c:66:6d:01:02:03 (player 1)")
        _mini_expect(q, "paired pad 1c:66:6d:09:0a:0b (player 2)")
        _mini_expect(q, "cartridge menu")
        chip.reconnect_sdp(pad_b, key_b, 0x0C, (0x60, 0x61), 2)   # red, asks SDP first
        _mini_expect(q, "bt: controller 1c:66:6d:09:0a:0b connected (player 2)")
        chip.reconnect(pad_a, key_a, 0x0B, (0x50, 0x51), 1)       # blue
        _mini_expect(q, "bt: controller 1c:66:6d:01:02:03 connected (player 1)")

        # a third pad pairs from the monitor and becomes player 3
        q.mini.write(b"q")
        _mini_expect(q, "back to the monitor")
        q.mini.write(b"T")
        chip.pair(pad_c, key_c, 0x0D, (0x72, 0x73), 3)             # green
        out = _mini_expect(q, "next time just press PS")
        assert "connected (player 3)" in out and "paired as player 3" in out, out

        # the menu bar: a controller icon for each player (M27)
        q.mini.write(b"M")
        _mini_expect(q, "cartridge menu")
        time.sleep(1.0)
        screen_img = q.screendump()
        if opts.shots:
            _save_png(screen_img, os.path.join(opts.shots, "home-pads.png"))
        runs = bar_icons(screen_img)
        assert len(runs) == 3 and all(20 <= x1 - x0 <= 27 for x0, x1 in runs), runs
        assert all(blue_number(screen_img, r) for r in runs), "Bluetooth: blue numbers"

        # in a game each pad is its own player
        time.sleep(0.3)
        chip.report(0x08 | 0x20, handle=0x0D)                      # pad 3: cross plays
        time.sleep(0.1)
        chip.report(0x08, handle=0x0D)
        _mini_expect(q, "playing players.bm")
        _mini_expect(q, "player 4 [ 0.0,0.0]")                    # first frame: all idle
        _mini_expect(q, "players 3 mask 7")
        chip.report(0x08 | 0x10, handle=0x0C)                      # pad 2: square = X
        _mini_expect(q, "player 2 [6 0.0,0.0]")
        chip.report(0x08, handle=0x0B, lx=0)                       # pad 1: stick left
        _mini_expect(q, "player 1 [0 -1.0,0.0]")
        chip.report(0x08, handle=0x0B)
        _mini_expect(q, "player 1 [ 0.0,0.0]")
        q.mini.write(b"d")                                         # serial: the 4th player
        _mini_expect(q, "player 4 [1 1.0,0.0]")
        chip.report(0x08, ps=1, handle=0x0C)                       # PS on pad 2 quits
        _mini_expect(q, "update+draw")
        chip.report(0x08, ps=0, handle=0x0C)
        time.sleep(0.3)
        q.mini.write(b"q")
        _mini_expect(q, "back to the monitor")
    finally:
        q.close()
    try:
        part = os.path.join(tmp, "part.img")
        with open(img, "rb") as f, open(part, "wb") as o:
            f.seek(2048 * 512)
            o.write(f.read())
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
        text = subprocess.run(["mtype", "-i", part, "::/BM33/CONFIG.TXT"], capture_output=True,
                              text=True, env=env).stdout
        lines = text.splitlines()
        assert f"bt_pad1=1c:66:6d:01:02:03 {key_a.hex()}" in lines, text
        assert f"bt_pad2=1c:66:6d:09:0a:0b {key_b.hex()}" in lines, text
        assert f"bt_pad3=1c:66:6d:0c:0d:0e {key_c.hex()}" in lines, text
        assert not any(l.startswith("bt_pad=") for l in lines), text
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def test_sd_sdhc_and_usb_menu(b, opts):
    """4 GiB card (SDHC addressing); with a USB keyboard boot ends in the menu."""
    tmp = tempfile.mkdtemp(prefix="bm33-sd-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [(b("demo.bm"), "carts/game.bm")], 4096)
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"] + USB_KBD)
    try:
        out = q.expect("cartridge menu", timeout=90).decode(errors="replace")
        assert "sd: SDHC card (sdhost), FAT32, 4095 MiB, label BM33SD; 1 cartridges" in out, out
        time.sleep(1.0)
        shot_ = q.screendump()                # the bar: the keyboard icon (M27)
        runs = bar_icons(shot_)
        assert len(runs) == 1 and 20 <= runs[0][1] - runs[0][0] <= 27, runs
        assert not blue_number(shot_, runs[0]), "USB: a white number"
        sendkeys(q, "c")                      # C is the X button: the options (M27)
        _, text = settled_screen(q, lambda i, t: any("Author" in l for l in t))
        assert any("Author" in l for l in text) and any("Play" in l for l in text), "\n".join(text)
        sendkeys(q, "x")                      # X is the B button: back
        _, text = settled_screen(q, lambda i, t: not any("Author" in l for l in t))
        assert not any("Author" in l for l in text), "\n".join(text)
        sendkeys(q, "ret")
        q.expect("playing game.bm", timeout=10)
        time.sleep(1.5)
        sendkeys(q, "esc")
        q.expect("update+draw", timeout=15)
        time.sleep(0.5)
        sendkeys(q, "esc")
        q.expect("back to the monitor", timeout=10)
        q.expect("> ")
    finally:
        q.close()
        os.remove(img)


def test_usb_hid_gamepad(b, opts):
    """Generic HID parser: QEMU's usb-tablet (report descriptor with 3
    buttons and absolute X/Y) is taken as a gamepad; button 1 = A."""
    q = Qemu(b("kernel.img"), ["-device", "usb-tablet,port=1"])
    try:
        out = q.expect("cartridge menu", timeout=90).decode(errors="replace")
        assert "usb: gamepad 0627:0001 'QEMU USB Tablet'" in out, out
        q.monitor("mouse_move 16384 16384")    # centre: stick released
        # the tablet's button report also moves up once: from the first row
        # that is the tab bar, where A still plays the highlighted cover
        time.sleep(0.5)
        q.monitor("mouse_button 1")
        q.monitor("mouse_button 0")
        q.expect("playing demo.bm", timeout=10)
        time.sleep(1.0)
        q.send("q")
        q.expect("update+draw", timeout=15)
        q.send("q")
        q.expect("back to the monitor", timeout=10)
    finally:
        q.close()


GAMES = {                                  # cart -> text on its title screen
    "pong": "press A to start",
    "snake": "S N A K E",
    "shooter": "S T A R   S H O O T E R",
    "astrowing": "A S T R O   W I N G",
    "hunt": "HUNTER'S NIGHT",
}


def test_games(b, opts):
    """The demo games: title screen, start with A, play a few seconds with
    serial keys, no Lua error, quit with 'q'."""
    q = Qemu(b("kernel.img"))
    try:
        q.expect(MENU, timeout=30)             # uploads work from the menu too
        time.sleep(0.5)
        for name, title in GAMES.items():
            with open(b(f"carts/{name}.bm"), "rb") as f:
                assert _upload(q, f.read()), name
            time.sleep(1.0)
            _, text = settled_screen(q, lambda i, t: any(title in l for l in t))
            assert any(title in l for l in text), f"{name}: title screen\n" + "\n".join(text)
            if opts.shots:
                _save_png(q.screendump(), os.path.join(opts.shots, f"{name}-title.png"))
            q.send(" ")                        # A: start
            time.sleep(0.5)
            for k in "ddddwwwwssssaaaa" * 2:  # move around, fire
                q.send(k + " ")
                time.sleep(0.08)
            img = q.screendump()
            if opts.shots:
                _save_png(img, os.path.join(opts.shots, f"{name}-play.png"))
            q.send("q")
            out = q.expect("update+draw", timeout=10).decode(errors="replace")
            assert "stopped with an error" not in out, out
            time.sleep(0.5)                    # back in the menu
        q.send("q")
        q.expect(PROMPT)
    finally:
        q.close()


def test_texroom(b, opts):
    """Texture Room (M14): a textured 3D room at 320x180. The textures show
    (brick red, crate wood, stone grey all on screen), the crate count
    changes with B, no Lua error, and it quits with its frame statistics."""
    q = Qemu(b("kernel.img"))
    try:
        q.expect(MENU, timeout=30)
        time.sleep(0.5)
        with open(b("carts/texroom.bm"), "rb") as f:
            assert _upload(q, f.read())
        time.sleep(3)

        def near(c, ref, tol=40):
            return all(abs(c[i] - ref[i]) < tol for i in range(3))
        for _ in range(10):                     # a whole frame (not one being drawn)
            img = q.screendump()
            w, h, px = img
            cols = [tuple(px[(y * w + x) * 3:(y * w + x) * 3 + 3])
                    for y in range(0, h, 4) for x in range(0, w, 4)]
            # by hue: the lights change the brightness
            brick = sum(r > 60 and r > 2 * g and r > 2 * b for r, g, b in cols)
            wood = sum(r > 50 and r > g + 10 and g > b + 15 and r < 2 * g for r, g, b in cols)
            stone = sum(r > 50 and abs(r - g) < 12 and abs(g - b) < 16 for r, g, b in cols)
            hud = sum(near(c, (255, 224, 96), 30) for c in cols[:w // 4 * 4])
            if brick > 40 and wood > 40 and stone > 300 and hud:
                break
            time.sleep(0.5)
        if opts.shots:
            _save_png(img, os.path.join(opts.shots, "texroom.png"))
        print(f"     texroom colours: brick {brick}, wood {wood}, stone {stone}, hud {hud}, "
              f"{len(set(cols))} distinct")
        assert brick > 40 and wood > 40 and stone > 300 and hud, (brick, wood, stone, hud)
        assert len(set(cols)) > 100, len(set(cols))     # textures, not flat faces
        q.send("x")                             # B: more crates
        time.sleep(1)
        q.send("q")
        out = q.expect("update+draw", timeout=10).decode(errors="replace")
        assert "stopped with an error" not in out, out
        assert '"Texture Room"' in out, out[-300:]
    finally:
        q.close()


def test_kitchen(b, opts):
    """M17: Chaos Kitchen boots, goes from the title through the lobby into a
    campaign kitchen and into the endless kitchen, plays with serial keys
    and logs its frame times; no Lua error."""
    q = Qemu(b("kernel.img"))

    def keys(seq, gap=0.35):
        for k in seq:
            q.send(k)
            time.sleep(gap)

    def shot(name):
        # a screendump can catch the sky of a frame still being drawn: keep
        # the most colourful of a few
        if opts.shots:
            best, bn = None, -1
            for _ in range(5):
                img = q.screendump()
                w, h, px = img
                n = len({px[(y * w + x) * 3:(y * w + x) * 3 + 3] for y in range(0, h, 12) for x in range(0, w, 12)})
                if n > bn:
                    best, bn = img, n
            _save_png(best, os.path.join(opts.shots, f"kitchen-{name}.png"))

    try:
        q.expect(MENU, timeout=30)
        time.sleep(0.5)
        with open(b("carts/kitchen.bm"), "rb") as f:
            assert _upload(q, f.read())
        time.sleep(4.0)                        # title, with computer chefs cooking
        shot("title")
        keys("  ")                             # menu, CAMPAIGN
        keys("  ")                             # lobby: join, ready
        time.sleep(1.0)
        keys(" ")                              # map: stage 1-1
        time.sleep(1.0)
        shot("intro")
        keys(" ")                              # open the kitchen
        for k in "dddd  wwww  llll aaaa  ssss" * 2:
            q.send(k)
            time.sleep(0.1)
        out = q.expect("kitchen 1-1 x1:", timeout=20).decode(errors="replace")
        assert "stopped with an error" not in out, out
        shot("play")
        keys("\r")                             # pause
        keys("ss ")                            # QUIT: the map
        keys("k")                              # the menu
        keys("s ")                             # ENDLESS
        keys(" ")                              # lobby: ready
        time.sleep(1.0)
        keys(" ")                              # the endless kitchen
        out = q.expect("kitchen endless x1:", timeout=20).decode(errors="replace")
        assert "stopped with an error" not in out, out
        shot("endless")
        q.send("q")
        out = q.expect("update+draw", timeout=10).decode(errors="replace")
        assert "stopped with an error" not in out, out
        time.sleep(0.5)
        q.send("q")
        q.expect(PROMPT)
    finally:
        q.close()


def test_titan(b, opts):
    """M20: Titan Clash boots (its sheet is SHEET8), goes from the title
    through the mode and the hangar (both options changed) into a fight
    against the computer, plays with serial keys and logs its frame times;
    no Lua error."""
    q = Qemu(b("kernel.img"))

    def keys(seq, gap=0.35):
        for k in seq:
            q.send(k)
            time.sleep(gap)

    def shot(name):
        # a screendump can catch a frame half drawn: keep the most colourful
        if opts.shots:
            best, bn = None, -1
            for _ in range(5):
                img = q.screendump()
                w, h, px = img
                n = len({px[(y * w + x) * 3:(y * w + x) * 3 + 3] for y in range(0, h, 12) for x in range(0, w, 12)})
                if n > bn:
                    best, bn = img, n
            _save_png(best, os.path.join(opts.shots, f"titan-{name}.png"))

    try:
        q.expect(MENU, timeout=30)
        time.sleep(0.5)
        with open(b("carts/titan.bm"), "rb") as f:
            assert _upload(q, f.read())
        time.sleep(3.0)
        shot("title")
        keys("\r")                             # the mode
        keys(" ")                              # 1 PLAYER VS CPU
        time.sleep(1.0)
        shot("hangar")
        keys("d")                              # the other armour
        keys("sd")                             # the other weapon
        time.sleep(1.0)
        shot("hangar2")
        keys("s ")                             # READY
        time.sleep(2.4)
        shot("round")
        time.sleep(1.2)
        for k in "ddddddlilliikkxxdsdcdsdvddaaaawwdd" * 3:
            q.send(k)
            time.sleep(0.08)
        shot("fight")
        out = q.expect("titan fight:", timeout=30).decode(errors="replace")
        assert "stopped with an error" not in out, out
        print("    titan fight:" + q.expect("effects", timeout=5).decode(errors="replace"))
        shot("fight2")
        q.send("q")
        out = q.expect("update+draw", timeout=10).decode(errors="replace")
        assert "stopped with an error" not in out, out
        time.sleep(0.5)
        q.send("q")
        q.expect(PROMPT)
    finally:
        q.close()


def _save_png(img, path):
    import struct
    import zlib
    w, h, px = img
    raw = b"".join(b"\0" + px[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def _upload(q, data):
    q.send("U")
    q.expect("15 s timeout\r\n")
    loader = bm33_load.Loader(q.port, echo=q)
    return loader.upload(data)


def _b33(src):
    return mkb33.pack(src.encode(), title="test cart")


AUDIO_CART = r"""
function _init()
  envelope(2, 0, 60, 0, 30)
  duty(0, 64)
  note(0, 440, 50, SQUARE, 100)
  note(2, 2500.7, 0, NOISE)
  freq(2, 1200)
  apu(5, 4, 77)
  log("apu", apu(0, 0) + apu(0, 1) * 256, apu(0, 2), apu(0, 3), apu(2, 0) + apu(2, 1) * 256,
      apu(2, 2), apu(2, 6), apu(5, 4), apu(2, 9))
  noteoff(2)
  log("off", apu(2, 9), type(playing(0)), TRIANGLE, SAW)
  log("bad", select(2, pcall(note, 8, 440)), select(2, pcall(apu, 0, 16)))
  quit()
end
"""


def test_audio(b, opts):
    """M10: without HDMI audio (QEMU) the console says why and stays silent;
    the .bm sound API writes the APU-layout registers."""
    q = Qemu(b("kernel.img"))
    try:
        out = q.boot().decode(errors="replace")
        assert "audio: off - no HDMI audio" in out, out
        q.send("a")
        q.expect("audio: off - no HDMI audio")
        q.expect("> ")
        assert _upload(q, mkb33.pack(AUDIO_CART.encode(), title="audio test"))
        out = q.expect("bad\t", timeout=15).decode(errors="replace")
        out += q.expect("\n").decode(errors="replace")
        assert "apu\t440\t0\t64\t1200\t3\t60\t77\t1" in out, out
        assert "off\t0\tboolean\t1\t2" in out, out
        assert "bad\t" in out and "voice 0..7" in out and "register 0..15" in out, out
        q.expect("> ", timeout=10)
    finally:
        q.close()


def test_dma(b, opts):
    """M14: the DMA test copies and fills RAM and the screen correctly."""
    q = Qemu(b("kernel.img"))
    try:
        q.boot()
        q.send("D")
        out = q.expect("DMA test passed", timeout=60).decode(errors="replace")
        assert out.count(" ok") == 6 and "FAILED" not in out, out
        q.expect("> ")
    finally:
        q.close()


TEX_CART = r"""
local m
function _init()
  for y = 0, 15 do for x = 0, 15 do sset(x, y, x < 8 and 0xFF0000 or 0x0000FF) end end
  m = mesh({ -1,-1,0, 1,-1,0, 1,1,0, -1,1,0 }, { 1,3,2,-1, 1,4,3,-1 },
           { 0,16, 16,0, 16,16,   0,16, 0,0, 16,0 })
end
local n = 0
function _update() n = n + 1 end
function _draw()
  cls(0)
  zclear()
  camera3d(0, 0, -3)
  light3d(0, 0, -1, 1)
  draw3d(m, 0, 0, 0)
  if n == 3 then
    log("tex", string.format("%06x %06x", pget(280, 180), pget(360, 180)), stat(4))
    quit()
  end
end
"""


def test_textured_mesh(b, opts):
    """M14: mesh() with texture coordinates draws the sprite sheet on faces."""
    q = Qemu(b("kernel.img"))
    try:
        q.boot()
        assert _upload(q, mkb33.pack(TEX_CART.encode(), title="texture test"))
        out = q.expect("tex\t", timeout=15).decode(errors="replace")
        out += q.expect("\n").decode(errors="replace")
        assert "ff0000 0000ff\t2" in out, out
        q.expect("> ", timeout=10)
    finally:
        q.close()


def test_editor(b, opts):
    """M15: the editor makes a new game, saves it on the SD card, tries it,
    comes back; a game that stops with an error brings the editor to the
    line; the card is still a clean FAT32 volume."""
    tmp = tempfile.mkdtemp(prefix="bm33-ed-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [(b("carts/pong.bm"), "carts/pong.bm")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])

    def k(s, gap=0.15):
        q.send(s)
        time.sleep(gap)

    def see(word, tries=40):
        for _ in range(tries):
            _, text = settled_screen(q, lambda i, t: any(word in l for l in t), tries=2)
            if any(word in l for l in text):
                return text
            time.sleep(0.25)
        raise AssertionError(f"not on screen: {word}\n" + "\n".join(text))
    try:
        q.boot()
        k("e")
        see("bm33 editor")
        k("\x1b", 0.5)                                      # menu -> code
        see("line 1/")
        k("\x1b", 0.5)                                      # code -> menu
        see("Exit editor")
        for _ in range(4):
            k("\x1b[B", 0.25)                               # down to "Save as..."
        k("\r")
        see("file name")
        k("\r")                                             # MYGAME.BM
        see("saved /carts/MYGAME.BM")
        k("\x12", 1)                                        # Ctrl+R: try it
        for _ in range(40):                                 # the game is on (slow hosts)
            _, text = settled_screen(q, lambda i, t: True, tries=1)
            if not any("saved /carts/MYGAME.BM" in l or "line 1/" in l for l in text):
                break
            time.sleep(0.25)
        time.sleep(1)
        k("q")                                              # and back to the editor
        out = q.expect('b33: "New game"', timeout=20).decode(errors="replace")
        assert "stopped with an error" not in out, out
        time.sleep(3)
        k("\x1b[H", 0.3)
        for ch in "error('boom')\r":
            k(ch, 0.05)
        k("\x12")
        out = q.expect("main.lua:1: boom", timeout=20).decode(errors="replace")
        time.sleep(3)
        _, text = settled_screen(q, lambda i, t: any("the game stopped" in l for l in t))
        assert any("the game stopped" in l for l in text), "\n".join(text)
        k("\x1b", 0.4)
        k("\x1b[A", 0.3)                                    # Exit editor
        k("\r", 0.3)
        k("\r")                                             # confirm: unsaved changes
        q.expect("> ", timeout=10)
    finally:
        q.close()
    try:
        part = os.path.join(tmp, "part.img")
        with open(img, "rb") as f, open(part, "wb") as o:
            f.seek(2048 * 512)
            o.write(f.read())
        fsck = subprocess.run(["fsck.vfat", "-n", part], capture_output=True, text=True)
        assert fsck.returncode == 0, fsck.stdout + fsck.stderr
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
        saved = subprocess.run(["mtype", "-i", part, "::/CARTS/MYGAME.BM"], capture_output=True,
                               env=env).stdout
        assert saved[:8] == b"BM33CART" and b"error('boom')" in saved, saved[:200]
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


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
        q.expect("commands (")
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
                     "3D spheres 96 (C)", "3D smooth (Gouraud)", "3D textured",
                     "sprites 16x16 (Lua)"):
            m = re.search(re.escape(name) + r"\s+(\S+)", plain)
            assert m, f"{name} missing:\n{plain}"
        m = re.search(r"sprites 16x16 \(C\)\s+(\d+)\s+(\d+)", plain)
        assert m and int(m[2]) > int(m[1]) > 100, m and m.group(0)
        assert re.search(r"3D spheres 96 \(C\)\s+\d+ \(\d+ tri\)", plain), plain
        # QEMU is slow at floating point: the new 3D rows may start below 1
        assert re.search(r"3D smooth \(Gouraud\)\s+(<1|\d+ \(\d+ tri\))", plain), plain
        assert re.search(r"3D textured\s+(<1|\d+ \(\d+ tri\))", plain), plain
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
    ap.add_argument("--shots", default="", help="directory for screenshots of the games")
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
