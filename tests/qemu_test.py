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
import struct
import subprocess
import sys
import tempfile
import time
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, os.path.join(HERE, "..", "scripts"))
import bm_load  # noqa: E402
import bmmesh  # noqa: E402
import mkbm  # noqa: E402
import mksd  # noqa: E402

QEMU = os.environ.get("QEMU", "qemu-system-arm")
REF_DIR = os.path.join(HERE, "ref")
PROMPT = b"type 'h' for help"
BM_DEMO = b"native demo cart"
MENU = b"cartridge menu"


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Qemu:
    def __init__(self, image, extra=(), mini_uart=False, machine="raspi0"):
        """mini_uart: the second serial port (the mini UART, where the
        console goes when the PL011 is given to Bluetooth) on a socket too,
        as self.mini. machine: raspi1ap is a Pi 1 A+ (same SoC)."""
        self.tmp = tempfile.mkdtemp(prefix="bm-")
        self.mon_path = os.path.join(self.tmp, "mon.sock")
        tcp, tcp2 = free_port(), free_port()
        self.proc = subprocess.Popen(
            [QEMU, "-M", machine, "-bios", image, "-display", "none",
             "-serial", f"tcp:127.0.0.1:{tcp},server=on,wait=on",
             "-serial", f"tcp:127.0.0.1:{tcp2},server=on,wait=on" if mini_uart else "null",
             "-monitor", f"unix:{self.mon_path},server=on,wait=off", *extra],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        self.port = bm_load.Port(f"tcp:127.0.0.1:{tcp}", 115200)
        self.mini = bm_load.Port(f"tcp:127.0.0.1:{tcp2}", 115200) if mini_uart else None
        self.buf = b""

    # file-like sink so bm_load.Loader can echo into our buffer
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
        out = self.expect(BM_DEMO, timeout=20)
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


def load_font(cw=8, ch=16):
    """Glyph bitmaps from src/gfx/font<cw>x<ch>.c, keyed by row bytes."""
    import re
    src = open(os.path.join(HERE, "..", "src", "gfx", "font%dx%d.c" % (cw, ch))).read()
    rows = re.findall(r"\{ (0x[0-9a-f]{2}(?:, 0x[0-9a-f]{2}){%d}) \}" % (ch - 1), src)
    table = {}
    for code, r in enumerate(rows):
        key = bytes(int(b, 16) for b in r.split(", "))
        table.setdefault(key, bytes([code]).decode("cp437"))
    return table


FONTS = {}


def screen_text(img, cw=8, ch=16):
    """Reads the console back from a screendump: one string per text row,
    with the font of that cell size (8x16, or 6x12 for bm Code). Unknown
    cells become '?'."""
    if (cw, ch) not in FONTS:
        FONTS[(cw, ch)] = load_font(cw, ch)
    font = FONTS[(cw, ch)]
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
                line.append(font.get(bits, "?"))
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
        for s in (b"bm\x1b[0m kernel", b"board 920092", b"screen 640x360",
                  b"double buffer on", b"sd: no card", b"usb: nothing attached"):
            assert s in out, f"missing {s!r} in boot log"
        hz = int(re.search(rb"measured (\d+) Hz", out).group(1))
        assert 900 <= hz <= 1100, f"timer IRQ rate {hz} Hz"  # QEMU host jitter
        plain = re.sub(rb"\x1b\[[0-9;]*m", b"", out).decode(errors="replace")
        for s in ("MMU+caches on", "console 80x21", "benchmark (us)",
                  "libc selftest: ok", "printf 3.142, sqrt(2) 1.414213562",
                  "IRQ on: timer 1000 Hz", "vsync probe"):
            assert s in plain, f"missing {s!r} in boot log"
        text = "\n".join(screen_text(q.screendump()))
        for s in ("Lua 5.4 on bm", "2^10=1024.0 7//2=3 sqrt(2)=1.414214 THE QUICK BROWN FOX co:1,4,9",
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
        # the menu: tabs at the top, button hints at the bottom (near white);
        # nothing pressed yet: the DS4's buttons, Share + Options for the monitor
        _, text = settled_screen(q, lambda i, t: "Monitor" in t[21])
        img = q.screendump()
        text = screen_text(img)
        assert "Games" in text[1] and "Dev" in text[1], text[1]
        col = text[21].index("Monitor")
        colours = {pixel(img, x, 21 * 16 + y) for x in range(col * 8, col * 8 + 56) for y in range(16)}
        assert any(r > 230 and g > 230 and b > 230 for r, g, b in colours), \
            f"hint text not rendered {colours}"
        spans = prompt_spans(img, 21)
        assert len(spans) == 3 and spans[-1][1] - spans[-1][0] >= 44, spans   # cross, Share, Options
        if opts.shots:
            _save_png(img, os.path.join(opts.shots, "menu-hints.png"))
        q.send("q")
        q.expect(PROMPT)
        q.expect("> ")
        for _ in range(10):                 # the uptime appears once the monitor waits
            img = q.screendump()
            text = screen_text(img)
            if "up 00:00:" in text[0]:
                break
            time.sleep(0.2)
        assert text[0].startswith(" bm "), f"status bar: {text[0]!r}"
        assert "up 00:00:0" in text[0], f"uptime in status bar: {text[0]!r}"
        assert pixel(img, 2, 2) == (0, 170, 170), "status bar colour"
        # scrolling: 40 unknown-command lines push the prompt text off screen
        for _ in range(20):
            q.send("x")
            q.expect("> ")
        text = screen_text(q.screendump())
        assert not any("back to the monitor" in l for l in text), "console did not scroll"
        assert text[0].startswith(" bm "), "status bar scrolled away"
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
        # the log has no colours: the banner's line is plain up to the reset
        # the pager puts at the end of every line (not a fixed 40 characters:
        # that depended on the length of the version)
        line = out.split("bm kernel")[1].split("\r\n")[0] if "bm kernel" in out else "\x1b"
        assert "\x1b" not in line.removesuffix("\x1b[0m"), out[-300:]
        q.send("\x1b")                       # Esc alone returns
        q.expect("> ", timeout=5)
        _, text = settled_screen(q, lambda i, t: any(l.startswith(">") for l in t))
        assert text[0].startswith(" bm "), text[0]
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
            if top.startswith(" bm M4 demo"):
                break
        assert top.startswith(" bm M4 demo") and "fps" in top, f"demo overlay: {top!r}"
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
        assert any(screen_text(q.screendump())[0].startswith(" bm M4 demo")
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
        q.send("bm.millis() > 0, math.type(bm.micros())\r")
        assert b"true\tinteger" in q.expect("lua> ")
        q.send("string.format('%.3f %5.1f', math.pi, 2.25)\r")
        assert b"3.142   2.2" in q.expect("lua> ")
        q.send("select(2, bm.mem()) > 0\r")
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


BM_COLOURS = [(248, 0, 0), (0, 252, 0), (0, 0, 248), (248, 252, 248)]


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


def prompt_spans(img, row):
    """The button prompts on a text row of the menu's hints (prompts.c):
    spans [x0, x1) of their grey lip, the second-last pixel row of the cell
    (the labels' white text never has that colour)."""
    runs, start, y = [], None, row * 16 + 14
    for x in range(640):
        r, g, b = pixel(img, x, y)
        lip = 120 <= r <= 160 and abs(r - g) < 8 and 8 <= b - r <= 24
        if lip and start is None:
            start = x
        elif not lip and start is not None:
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


def scroll_thumb(img):
    """The thumb of the menu's scroll bar (more than two rows of covers):
    (top, bottom) of its light pixels at x 619, y 112-288, or None."""
    ys = [y for y in range(112, 288) if sum(pixel(img, 619, y)) > 600]
    return (ys[0], ys[-1] + 1) if ys else None


def tabs_lit(img):
    """Which tabs of the menu bar are on their light pill (M27): Games, Dev,
    Settings, from a pixel of the pill left of each name."""
    return [name for name, x in (("Games", 20), ("Dev", 92), ("Settings", 148))
            if sum(pixel(img, x, 24)) > 600]


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


def test_bm_boot_demo(b, opts):
    q = Qemu(b("kernel.img"))
    try:
        q.boot()
        q.send("B")
        q.expect(BM_DEMO, timeout=25)
        out = q.expect("bm bench (", timeout=20)
        time.sleep(3.0)
        img, text = settled_screen(q, lambda i, t: t[0].startswith("bm native") and "sprites" in t[-1])
        assert img[:2] == (640, 360), img[:2]
        assert text[0].startswith("bm native .bm") and "fps" in text[0], text[0]
        assert "sprites" in text[-1] and "attract" in text[-1], text[-1]
        got = [pixel(img, 640 - 80 + i * 20 + 8, 8) for i in range(4)]
        assert got == BM_COLOURS, f"RGB565 colour check {got}"
        out = q.expect("Lua memory:", timeout=30).decode(errors="replace")
        m = re.search(r'bm: "bm native demo" (\d+) frames, ([\d.]+) fps', out)
        assert m and 850 <= int(m[1]) <= 920 and 55 <= float(m[2]) <= 62, out
        assert re.search(r"update\+draw avg [\d.]+ ms", out), out
        q.expect("> ")
        assert q.screendump()[:2] == (640, 360)
    finally:
        q.close()


def test_bm_keys(b, opts):
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
        sendkeys(q, "esc")                     # Esc alone: back, still the menu
        time.sleep(1.0)
        q.buf += q.port.read(0.3)
        assert b"back to the monitor" not in q.buf, q.buf.decode(errors="replace")
        sendkeys(q, "ctrl-esc")                # Ctrl+Esc: from the menu to the monitor
        q.expect("back to the monitor", timeout=10)
        q.expect(PROMPT, timeout=10)
        q.expect("> ")

        # bm: Esc quits the game
        sendkeys(q, "n")
        time.sleep(1.5)
        sendkeys(q, "esc")
        out = q.expect("update+draw", timeout=10).decode(errors="replace")
        m = re.search(r'demo" (\d+) frames', out)
        assert m and int(m[1]) < 300, out
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


def test_usb_hub(b, opts):
    """Devices behind a hub (the Pi 1 B's USB ports are all behind its
    LAN951x): each port reset and enumerated; the keyboard wins over the
    tablet (a gamepad) and types. QEMU's hub is full speed, so no split
    transactions here: those need a high-speed hub (the LAN951x)."""
    hub = ["-device", "usb-hub,port=1", "-device", "usb-kbd,port=1.2",
           "-device", "usb-tablet,port=1.4"]
    q = Qemu(b("kernel.img"), hub)
    try:
        out = q.expect(MENU, timeout=40)
        assert b"usb: port 2: if0 class 03/01/01" in out, out
        assert b"usb: port 4: if0 class 03/00/00" in out, out
        assert b"usb: hub 0409:55aa, 8 ports, full speed" in out, out
        assert (b"usb: keyboard 0627:0001 'QEMU USB Keyboard', full speed, layout it "
                b"(hub port 2)") in out, out
        time.sleep(0.5)
        sendkeys(q, "ctrl-esc")                # Ctrl+Esc: from the menu to the monitor
        q.expect(PROMPT, timeout=10)
        q.expect("> ")
        sendkeys(q, "l")
        q.expect("lua> ", timeout=10)
        sendkeys(q, "p r i n t shift-8 3 shift-bracket_right 4 shift-9 ret")
        q.expect("\n12\r\n", timeout=10)
        sendkeys(q, "esc")
        q.expect("> ", timeout=10)
        q.send("y")                             # scan again: same result
        out = q.expect("(hub port 2)", timeout=20)
        assert b"usb: hub 0409:55aa" in out, out
    finally:
        q.close()
    # only the tablet behind the hub: it is the gamepad
    q = Qemu(b("kernel.img"), ["-device", "usb-hub,port=1", "-device", "usb-tablet,port=1.3"])
    try:
        out = q.expect(MENU, timeout=40)
        assert b"usb: gamepad 0627:0001 'QEMU USB Tablet', full speed (hub port 3)" in out, out
    finally:
        q.close()


def test_pi1_board(b, opts):
    """The same kernel on a Pi 1 (QEMU's raspi1ap, a Pi 1 A+): the board is
    named, and WiFi, Bluetooth and Ethernet say they are not there."""
    q = Qemu(b("kernel.img"), USB_KBD, machine="raspi1ap")
    try:
        out = q.boot()
        assert b"kernel" in out and b"- Raspberry Pi 1 A+ (BCM2835, revision 900021)" in out, out
        assert b"usb: keyboard 0627:0001" in out, out
        q.send("W")
        q.expect("wifi: no WiFi on the Pi 1 A+", timeout=10)
        q.expect("> ")
        q.send("T")
        q.expect("bt: no Bluetooth on the Pi 1 A+", timeout=10)
        q.expect("> ")
        q.send("E")
        q.expect("eth: no Ethernet controller", timeout=10)
        q.expect("> ")
        q.send("i")
        q.expect("board revision : 00900021 (Raspberry Pi 1 A+)", timeout=10)
    finally:
        q.close()


def test_wifi_probe(b, opts):
    """M18: the SD card is on SDHOST, so the Arasan controller goes to the
    WiFi pins. QEMU has no WiFi chip: 'W' must stop at CMD5 with a clear
    message, without hanging, and the SD card must still work afterwards."""
    tmp = tempfile.mkdtemp(prefix="bm-wifi-")
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
    tmp = tempfile.mkdtemp(prefix="bm-sd-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [(b("demo.bm"), "Il mio gioco lungo.bm"),
                     (b("carts/pong.bm"), "carts/demo2.bm"),
                     (os.path.join(HERE, "..", "README.md"), "README.md")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    try:
        out = q.boot().decode(errors="replace")
        assert "sd: SD card (sdhost), FAT32, 127 MiB, label BMSD; 2 cartridges" in out, out
        q.send("f")
        out = q.expect("\"Pong\"\r\n").decode(errors="replace")
        assert "Il mio gioco lungo.bm" in out and "/carts/demo2.bm" in out, out
        q.expect("> ")
        q.send("M")
        q.expect("cartridge menu")
        time.sleep(0.5)
        _, text = settled_screen(q, lambda i, t: any("bm native demo" in l for l in t))
        screen = "\n".join(text)
        for s_ in ("Games", "bm native demo", "/Il mio gioco lungo.bm"):
            assert s_ in screen, screen
        q.send("d")                           # the next cover: its title and file
        _, text = settled_screen(q, lambda i, t: any("Pong" in l for l in t))
        screen = "\n".join(text)
        assert "Pong" in screen and "/carts/demo2.bm" in screen, screen
        q.send("a")
        q.send("\r")                          # SD cartridges come first, by title
        q.expect("playing Il mio gioco lungo.bm", timeout=10)
        time.sleep(1.0)
        q.send("q")
        out = q.expect("update+draw", timeout=15).decode(errors="replace")
        assert '"bm native demo"' in out, out
        q.send("d\r")                         # next: /carts/demo2.bm
        _, text = settled_screen(q, lambda i, t: any("Close bm native demo?" in l for l in t))
        assert any("Close bm native demo?" in l for l in text), "\n".join(text)
        q.send("\r")                          # the demo was suspended: close it
        q.expect("playing demo2.bm", timeout=10)
        time.sleep(1.0)
        q.send("q")
        q.expect("update+draw", timeout=15)
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
    tmp = tempfile.mkdtemp(prefix="bm-susp-")
    img = os.path.join(tmp, "sd.img")
    cart = os.path.join(tmp, "counter.bm")
    with open(cart, "wb") as f:
        f.write(mkbm.pack(COUNTER_CART.encode(), title="AAA counter"))
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
    tmp = tempfile.mkdtemp(prefix="bm-home-")
    img = os.path.join(tmp, "sd.img")
    saver = os.path.join(tmp, "saver.bm")
    with open(saver, "wb") as f:
        f.write(mkbm.pack(SAVER_CART.encode(), title="AAA saver", author="tests"))
    victim = os.path.join(tmp, "victim.bm")
    with open(victim, "wb") as f:
        f.write(mkbm.pack(COUNTER_CART.encode(), title="BBB delete me"))
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
        assert "bm" not in text.splitlines()[1] and "pads" not in text, text
        shot_ = q.screendump()
        assert bar_icons(shot_) == [], "no keyboard, pad or network: no icons"
        assert scroll_thumb(shot_) is None, "one row of covers: no scroll bar"
        shot("games")
        keys("\r")                              # play: it saves, then Esc suspends it
        q.expect("saver start", timeout=10)
        time.sleep(0.5)
        q.send("q")
        q.expect('"AAA saver" suspended', timeout=10)
        time.sleep(0.5)

        # the options of the suspended game
        keys("x")
        # (with five tools, Open in bm Mesh and the info rows are below: the panel scrolls)
        screen(["AAA saver", "Resume", "Close the game", "Open in the SDK", "Open in bm Code",
                "Open in the Sound editor", "Open in the 3D studio"])
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
        screen(["BBB delete me", "Play", "Open in the 3D studio"])
        keys("w")                               # up from the first: the last rows, the file too
        screen(["Delete from the SD card", "/carts/Un gioco da cancellare.bm"])
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
        keys("s")                               # the volume: left/right, saved
        screen(["Volume", "< 10 / 10 >"])
        keys("a")
        screen(["< 9 / 10 >", "volume: 9 / 10"])
        keys("d")
        screen(["< 10 / 10 >"])
        keys("s")
        keys("\r")
        screen(["Settings > System", "Version", "Board", "SD card", "FAT32"])
        shot("system")
        keys("w")                               # the list scrolls to its last rows
        screen(["Restart", "Open the monitor"])
        keys("q")
        keys("wwwww")                           # System -> Controllers
        keys("\r")
        screen(["Settings > Controllers", "Player 1", "keyboard / USB", "Bluetooth keyboard",
                "Pair a new controller"])
        keys("w")                               # the list scrolls to its last row
        screen(["Pair a keyboard", "Forget all controllers"])
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
        keys("2")
        screen(["bm SDK", "editor (built-in)"])
        keys("d")                               # the covers' names are on pictures: the pill
        screen(["bm Sound", "sound (built-in)"])
        keys("d")
        screen(["bm 3D studio", "3D studio (built-in)"])
        keys("d")
        screen(["bm Mesh", "mesh (built-in)"])
        keys("d")
        screen(["bm Pixel", "pixel (built-in)"])
        keys("d")
        screen(["Code", "code editor: tabs, two pages"])
        keys("d")
        screen(["Assistant", "help with code and sprites"])
        keys("d")
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
        saves = subprocess.run(["mdir", "-i", part, "::/BM/SAVE"], capture_output=True,
                               text=True, env=env).stdout
        assert not re.search(r"^[0-9A-F]{8}\s+SAV", saves, re.M), saves
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def test_make_image(b, opts):
    """`make image` (with placeholder firmware files): the SD image boots to
    a menu with the demo games, and one of them runs from the card."""
    tmp = tempfile.mkdtemp(prefix="bm-img-")
    fw = os.path.join(tmp, "fw")
    os.makedirs(fw)
    for n in ("bootcode.bin", "start.elf", "fixup.dat"):
        with open(os.path.join(fw, n), "wb") as f:
            f.write(b"placeholder")
    root = os.path.join(HERE, "..")
    subprocess.run(["make", "-s", "-C", root, "image", f"FW_DIR={fw}", f"DIST={tmp}",
                    f"BUILD={os.path.abspath(b('.'))}"], check=True, stdout=subprocess.DEVNULL)
    img = os.path.join(tmp, "bm.img")
    assert os.path.getsize(img) == 64 << 20, os.path.getsize(img)
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    try:
        out = q.expect(MENU, timeout=30).decode(errors="replace")
        assert "FAT32, 63 MiB, label BM; 10 cartridges" in out, out
        time.sleep(0.5)
        want = ("Pong", "Snake", "Star Shooter", "Chaos Kitchen", "Texture Room", "Studio Village", "nano8")
        titles = want + ("Astro Wing", "Titan Clash", "Hunter's Night")

        def chosen(t):                         # the name of the chosen cover (row 4)
            return next((n for n in titles if len(t) > 4 and n in t[4]), None)

        seen, rows, prev = set(), [], None
        for _ in range(20):                    # right along the grid (it wraps): each title in turn
            # a screendump can catch a frame half drawn (row 4 without its
            # title) or the cover before: wait for the next title
            _, text = settled_screen(q, lambda i, t: chosen(t) not in (None, prev), tries=20)
            rows.append(text[4] if len(text) > 4 else "")
            prev = chosen(text) or prev
            seen.add(prev)
            if all(w in seen for w in want):
                break
            q.send("d")
            time.sleep(0.3)
        screen = "\n".join(rows)
        for title in want:
            assert title in seen, screen
        for title in ("bm native demo", "bm stress test"):   # not games: in the kernel
            assert title not in screen, screen
        q.send("q")
        q.expect(PROMPT)
        q.expect("> ")
        q.send("f")
        out = q.expect('Star Shooter"\r\n').decode(errors="replace")
        assert "/carts/pong.bm" in out and "/carts/shooter.bm" in out, out
        q.close()

        # the Pi 1 image: same kernel and games, no WiFi/Bluetooth firmware
        with open(os.path.join(fw, "BCM43430A1.hcd"), "wb") as f:
            f.write(b"placeholder")
        subprocess.run(["make", "-s", "-C", root, "image", "image-pi1", f"FW_DIR={fw}",
                        f"DIST={tmp}", f"BUILD={os.path.abspath(b('.'))}"],
                       check=True, stdout=subprocess.DEVNULL)
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
        def ls(name):
            return subprocess.run(["mdir", "-i", f"{os.path.join(tmp, name)}@@1M", "-b", "-/", "::"],
                                  env=env, capture_output=True, text=True).stdout
        assert "::/BM/BCM43430A1.HCD" in ls("bm.img").upper(), ls("bm.img")
        # the carts nano8 plays, with their long names
        assert "::/carts/nano8/nanodemo.p8" in ls("bm.img"), ls("bm.img")
        assert "::/carts/nano8/starmoovalley.p8.png" in ls("bm.img"), ls("bm.img")
        pi1 = ls("bm-pi1.img")
        assert "::/KERNEL.IMG" in pi1.upper() and "BCM43430A1" not in pi1.upper(), pi1
        q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={os.path.join(tmp, 'bm-pi1.img')}"],
                 machine="raspi1ap")
        out = q.expect(MENU, timeout=30).decode(errors="replace")
        assert "Raspberry Pi 1 A+" in out and "; 10 cartridges" in out, out
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


def test_old_folder_and_carts(b, opts):
    """After the rename: a card from before (settings in /bm33, a cartridge
    with the old header) still works: the settings are read, the cartridge
    is listed with its title, and the next save goes to /bm."""
    tmp = tempfile.mkdtemp(prefix="bm-old-")
    img = os.path.join(tmp, "sd.img")
    cfg = os.path.join(tmp, "config.txt")
    with open(cfg, "w") as f:
        f.write("layout=us\nwifi_boot=0\n")
    old = os.path.join(tmp, "old.bm")
    data = bytearray(mkbm.pack(b"function _draw() cls(0) end", title="Old header"))
    data[0:8] = b"BM" + b"33" + b"CART"                # the magic of before
    with open(old, "wb") as f:
        f.write(data)
    mksd.build(img, [(cfg, "bm" + "33/config.txt"), (old, "carts/old.bm")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    try:
        out = q.boot().decode(errors="replace")
        assert "layout us" in out, out
        q.send("f")
        out = q.expect('"Old header"').decode(errors="replace")
        assert "/carts/old.bm" in out, out
        q.expect("> ")
        q.send("L")                             # layout: us -> it, saved
        q.expect("> ")
        time.sleep(0.5)
    finally:
        q.close()
    part = os.path.join(tmp, "part.img")
    with open(img, "rb") as f, open(part, "wb") as o:
        f.seek(2048 * 512)
        o.write(f.read())
    text = subprocess.run(["mtype", "-i", part, "::/BM/CONFIG.TXT"], capture_output=True, text=True,
                          env=dict(os.environ, MTOOLS_SKIP_CHECK="1")).stdout
    shutil.rmtree(tmp, ignore_errors=True)
    assert "wifi_boot=0" in text and "layout=it" in text, text


def test_sd_save_and_config(b, opts):
    """M11: a cart's save() and the monitor settings survive a reboot; the
    card is still a clean FAT32 volume afterwards (fsck.vfat, mtools)."""
    tmp = tempfile.mkdtemp(prefix="bm-save-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [(b("carts/snake.bm"), "carts/snake.bm")])
    cart = mkbm.pack(SAVE_CART.encode(), title="save test")
    drive = ["-drive", f"if=sd,format=raw,file={img}"]
    try:
        for run in (1, 2):
            q = Qemu(b("kernel.img"), drive)
            try:
                out = q.boot().decode(errors="replace")
                if run == 2:
                    assert "config: /bm/config.txt, layout us, .bm drawing direct" in out, out
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
        cfg = subprocess.run(["mtype", "-i", part, "::/BM/CONFIG.TXT"], capture_output=True,
                             text=True, env=env).stdout
        assert "layout=us" in cfg and "draw=direct" in cfg, cfg
        saves = subprocess.run(["mdir", "-b", "-i", part, "::/BM/SAVE"], capture_output=True,
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
    tmp = tempfile.mkdtemp(prefix="bm-bt-")
    img = os.path.join(tmp, "sd.img")
    hcd = os.path.join(tmp, "BCM43430A1.hcd")
    with open(hcd, "wb") as f:                         # two records, like the real file
        f.write(bytes([0x4C, 0xFC, 8]) + bytes(range(8)) + bytes([0x4E, 0xFC, 4, 0xFF, 0xFF, 0xFF, 0xFF]))
    mksd.build(img, [(hcd, "bm/BCM43430A1.hcd")])
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
    # the light colour bm gives each player (bt.c)
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
        self.cmd(0x2001)                                               # LE event mask
        self.cmd(0x2002, ret=bytes([27, 0, 8]))                        # LE buffers: 8 of 27 bytes

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

    def report(self, buttons=0x08, ps=0, handle=None, lx=128, ly=128, shoulders=0):
        """DS4 reduced input report 0x01 on the host's interrupt channel;
        shoulders: 1 = L1, 2 = R1."""
        self.l2(0x0041, bytes([0xA1, 0x01, lx, ly, 128, 128, buttons, shoulders, ps, 0, 0]), handle)


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
    tmp = tempfile.mkdtemp(prefix="bm-bt-")
    img = os.path.join(tmp, "sd.img")
    hcd = os.path.join(tmp, "BCM43430A1.hcd")
    with open(hcd, "wb") as f:
        f.write(bytes([0x4C, 0xFC, 4, 1, 2, 3, 4, 0x4E, 0xFC, 4, 0xFF, 0xFF, 0xFF, 0xFF]))
    mksd.build(img, [(hcd, "bm/BCM43430A1.hcd"), (b("carts/snake.bm"), "carts/snake.bm")])
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
        cfg = subprocess.run(["mtype", "-i", part, "::/BM/CONFIG.TXT"], capture_output=True,
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


class FakeMxKeys(FakeDs4Chip):
    """The chip plus a Bluetooth LE keyboard behind it (like a Logitech MX
    Keys S): advertising, LE connection, SMP pairing as responder (LE Secure
    Connections, Passkey Entry: it "types" the code bm shows), its identity
    key, a HID-over-GATT server, key reports as notifications; later it
    comes back from a resolvable private address."""

    ADDR_LE = bytes([0x21, 0x43, 0x65, 0x87, 0xA9, 0xCB | 0xC0])      # static random
    IDENTITY = bytes([0x56, 0x34, 0x12, 0x9E, 0x6D, 0x00])            # 00:6d:9e:12:34:56 public
    IRK = bytes(range(0x30, 0x40))
    LE_HANDLE = 0x0040
    REPORT_MAP = bytes([
        0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7,
        0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08,
        0x81, 0x01, 0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
        0x95, 0x01, 0x75, 0x03, 0x91, 0x01, 0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x26, 0xFF,
        0x00, 0x05, 0x07, 0x19, 0x00, 0x2A, 0xFF, 0x00, 0x81, 0x00, 0xC0,
        0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x03, 0x75, 0x10, 0x95, 0x02, 0x15, 0x01,
        0x26, 0xFF, 0x02, 0x19, 0x01, 0x2A, 0xFF, 0x02, 0x81, 0x00, 0xC0])

    def __init__(self, port):
        super().__init__(port)
        self.frames, self.rx = [], b""
        # the GATT database: handle -> (type, value)
        db = {0x10: (0x2800, (0x1812).to_bytes(2, "little"))}
        chars = [(0x11, 0x02, 0x2A4A, bytes([0x11, 0x01, 0x00, 0x03])),
                 (0x13, 0x02, 0x2A4B, self.REPORT_MAP),
                 (0x15, 0x1A, 0x2A4D, bytes(8)), (0x19, 0x0E, 0x2A4D, bytes(1)),
                 (0x1C, 0x12, 0x2A4D, bytes(4)), (0x20, 0x04, 0x2A4C, bytes(1)),
                 (0x22, 0x06, 0x2A4E, bytes([1]))]
        for h, props, uuid, value in chars:
            db[h] = (0x2803, bytes([props]) + (h + 1).to_bytes(2, "little") + uuid.to_bytes(2, "little"))
            db[h + 1] = (uuid, value)
        db.update({0x17: (0x2902, bytes(2)), 0x18: (0x2908, bytes([1, 1])), 0x1B: (0x2908, bytes([1, 2])),
                   0x1E: (0x2902, bytes(2)), 0x1F: (0x2908, bytes([3, 1]))})
        self.db, self.svc_end = db, 0x23

    # ---- transport
    def _next(self, want_cmd=None, want_cid=None, timeout=60):
        """Answers commands and collects L2CAP frames until the command
        `want_cmd` or a frame on `want_cid` comes; returns its parameters."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            for i, (cid, data) in enumerate(self.frames):
                if cid == want_cid:
                    del self.frames[i]
                    return data
            kind, a, payload = self.packet_any()
            if kind == "cmd":
                self.log.append((a, payload))
                if a in (0x200D, 0x2019, 0x0406):
                    self.status(a)
                else:
                    self._complete(a)
                if a == want_cmd:
                    return payload
            else:
                pb, data = a, payload
                self.rx = data if pb != 1 else self.rx + data
                if len(self.rx) >= 4 and len(self.rx) >= 4 + int.from_bytes(self.rx[:2], "little"):
                    n = int.from_bytes(self.rx[:2], "little")
                    self.frames.append((int.from_bytes(self.rx[2:4], "little"), self.rx[4:4 + n]))
                    self.rx = b""
        raise AssertionError(f"fake keyboard: timeout waiting for {want_cmd or want_cid:#x}")

    def packet_any(self):
        t = self._read(1, timeout=60)[0]
        if t == 0x01:
            op = int.from_bytes(self._read(2), "little")
            return "cmd", op, self._read(self._read(1)[0])
        assert t == 0x02, f"fake chip: packet type {t:#x}"
        hdr = self._read(4)
        h = int.from_bytes(hdr[:2], "little")
        assert h & 0x0FFF == self.LE_HANDLE, f"ACL on handle {h & 0xFFF:#x}"
        data = self._read(int.from_bytes(hdr[2:4], "little"))
        assert len(data) <= 27, f"host sent {len(data)} bytes in one packet (buffers are 27)"
        return "acl", (h >> 12) & 3, data

    def send_l2(self, cid, data, piece=None):
        frame = len(data).to_bytes(2, "little") + cid.to_bytes(2, "little") + data
        piece = piece or len(frame)
        for off in range(0, len(frame), piece):
            flag = 0x2000 if off == 0 else 0x1000
            chunk = frame[off:off + piece]
            self.port.write(bytes([0x02]) + (self.LE_HANDLE | flag).to_bytes(2, "little")
                            + len(chunk).to_bytes(2, "little") + chunk)

    def le_meta(self, sub, params):
        self._event(0x3E, bytes([sub]) + params)

    def advertise(self, addr, addr_type, data):
        self.le_meta(0x02, bytes([1, 0x00, addr_type]) + addr + bytes([len(data)]) + data + bytes([0xC8]))

    def connect(self, addr, addr_type):
        p = self._next(want_cmd=0x200D)
        assert p[5] == addr_type and p[6:12] == addr, p.hex()
        self.le_meta(0x01, bytes([0]) + self.LE_HANDLE.to_bytes(2, "little") + bytes([0, addr_type]) + addr
                     + bytes([0x0C, 0, 0, 0, 0xC8, 0, 0]))

    # ---- crypto (spec order: protocol bytes are least significant first)
    @staticmethod
    def _cmac(k, m):
        from Crypto.Hash import CMAC
        from Crypto.Cipher import AES
        c = CMAC.new(bytes(reversed(k)), ciphermod=AES)
        c.update(bytes(reversed(m)))
        return bytes(reversed(c.digest()))

    def f4(self, u, v, x, z):
        return self._cmac(x, bytes([z]) + v + u)

    def f5(self, w, n1, n2, a1, a2):
        salt = bytes.fromhex("6c888391aaf5a53860370bdb5a6083be")[::-1]
        t = self._cmac(salt, w)
        m = bytes([0, 1]) + a2 + a1 + n2 + n1 + bytes([0x65, 0x6C, 0x74, 0x62])
        return self._cmac(t, m + bytes([0])), self._cmac(t, m + bytes([1]))

    def f6(self, w, n1, n2, r, io, a1, a2):
        return self._cmac(w, a2 + a1 + io + r + n2 + n1)

    @staticmethod
    def ah(irk, prand):
        from Crypto.Cipher import AES
        r = bytes(reversed(prand + bytes(13)))
        return bytes(reversed(AES.new(bytes(reversed(irk)), AES.MODE_ECB).encrypt(r)))[:3]

    # ---- the keyboard's side
    def pair(self, host_addr, get_passkey):
        """Advertises in pairing mode, is connected, pairs (SC passkey)."""
        from Crypto.PublicKey import ECC
        # scan: parameters and enable, then the keyboard is seen
        self._next(want_cmd=0x200B)
        self._next(want_cmd=0x200C)
        adv = bytes([2, 0x01, 0x05, 3, 0x19, 0xC1, 0x03, 3, 0x03, 0x12, 0x18, 10, 0x09]) + b"MX Keys S"
        self.advertise(self.ADDR_LE, 1, adv)
        self.connect(self.ADDR_LE, 1)
        preq = self._next(want_cid=6)
        assert preq[0] == 0x01 and preq[1] == 0x00 and preq[3] & 0x0D == 0x0D, preq.hex()
        pres = bytes([0x02, 0x02, 0x00, 0x0D, 16, 0x00, 0x02])       # KeyboardOnly, SC; we give our IRK
        self.send_l2(6, pres)
        pk = self._next(want_cid=6)
        assert pk[0] == 0x0C and len(pk) == 65, pk.hex()
        pkax = pk[1:33]
        host_pub = ECC.construct(curve="P-256", point_x=int.from_bytes(pk[1:33], "little"),
                                 point_y=int.from_bytes(pk[33:65], "little"))
        key = ECC.generate(curve="P-256")
        pkbx = int(key.pointQ.x).to_bytes(32, "little")
        pkby = int(key.pointQ.y).to_bytes(32, "little")
        self.send_l2(6, bytes([0x0C]) + pkbx + pkby, piece=27)      # in pieces, like the chip
        dh = int((host_pub.pointQ * key.d).x).to_bytes(32, "little")
        passkey = get_passkey()
        for i in range(20):
            z = 0x80 | ((passkey >> i) & 1)
            ca = self._next(want_cid=6)
            assert ca[0] == 0x03, ca.hex()
            nb = os.urandom(16)
            self.send_l2(6, bytes([0x03]) + self.f4(pkbx, pkax, nb, z))
            na = self._next(want_cid=6)
            assert na[0] == 0x04, na.hex()
            assert self.f4(pkax, pkbx, na[1:], z) == ca[1:], f"round {i}: host confirm is wrong"
            self.send_l2(6, bytes([0x04]) + nb)
        a = host_addr + bytes([0])
        b_ = self.ADDR_LE + bytes([1])
        mackey, ltk = self.f5(dh, na[1:], nb, a, b_)
        r = passkey.to_bytes(16, "little")
        ea = self._next(want_cid=6)
        assert ea[0] == 0x0D and ea[1:] == self.f6(mackey, na[1:], nb, r, preq[1:4], a, b_), "bad Ea"
        self.send_l2(6, bytes([0x0D]) + self.f6(mackey, nb, na[1:], r, pres[1:4], b_, a))
        enc = self._next(want_cmd=0x2019)
        assert enc[12:28] == ltk and enc[2:12] == bytes(10), "host encrypts with another key"
        self._event(0x08, bytes([0]) + self.LE_HANDLE.to_bytes(2, "little") + bytes([1]))
        self.send_l2(6, bytes([0x08]) + self.IRK)
        self.send_l2(6, bytes([0x09, 0]) + self.IDENTITY)
        self.ltk = ltk

    @staticmethod
    def e(k, r):
        from Crypto.Cipher import AES
        return bytes(reversed(AES.new(bytes(reversed(k)), AES.MODE_ECB).encrypt(bytes(reversed(r)))))

    def c1(self, k, r, preq, pres, iat, ia, rat, ra):
        p1 = bytes([iat, rat]) + preq + pres
        p2 = ra + ia + bytes(4)
        t = self.e(k, bytes(a ^ b for a, b in zip(r, p1)))
        return self.e(k, bytes(a ^ b for a, b in zip(t, p2)))

    def pair_legacy(self, host_addr, get_passkey):
        """The same keyboard without Secure Connections: LE legacy pairing,
        passkey, STK, then its LTK/EDIV/Rand and identity."""
        self._next(want_cmd=0x200B)
        self._next(want_cmd=0x200C)
        adv = bytes([2, 0x01, 0x05, 3, 0x19, 0xC1, 0x03, 3, 0x03, 0x12, 0x18, 10, 0x09]) + b"MX Keys S"
        self.advertise(self.ADDR_LE, 1, adv)
        self.connect(self.ADDR_LE, 1)
        preq = self._next(want_cid=6)
        assert preq[0] == 0x01, preq.hex()
        pres = bytes([0x02, 0x02, 0x00, 0x05, 16, 0x00, 0x03])       # no SC; LTK and IRK
        self.send_l2(6, pres)
        tk = get_passkey().to_bytes(16, "little")
        mconfirm = self._next(want_cid=6)
        assert mconfirm[0] == 0x03, mconfirm.hex()
        srand = os.urandom(16)
        self.send_l2(6, bytes([0x03]) + self.c1(tk, srand, preq, pres, 0, host_addr, 1, self.ADDR_LE))
        mrand = self._next(want_cid=6)
        assert mrand[0] == 0x04
        assert self.c1(tk, mrand[1:], preq, pres, 0, host_addr, 1, self.ADDR_LE) == mconfirm[1:], "bad Mconfirm"
        self.send_l2(6, bytes([0x04]) + srand)
        stk = self.e(tk, mrand[1:9] + srand[:8])
        enc = self._next(want_cmd=0x2019)
        assert enc[12:28] == stk and enc[2:12] == bytes(10), "host encrypts with another STK"
        self._event(0x08, bytes([0]) + self.LE_HANDLE.to_bytes(2, "little") + bytes([1]))
        self.ltk, self.ediv, self.rand = os.urandom(16), 0x1234, bytes(range(1, 9))
        self.send_l2(6, bytes([0x06]) + self.ltk)
        self.send_l2(6, bytes([0x07]) + self.ediv.to_bytes(2, "little") + self.rand)
        self.send_l2(6, bytes([0x08]) + self.IRK)
        self.send_l2(6, bytes([0x09, 0]) + self.IDENTITY)

    def serve_gatt(self):
        """Answers the host's GATT client until it turns notifications on."""
        while True:
            req = self._next(want_cid=4)
            op = req[0]
            if op == 0x06:                                           # find by type value
                assert req[5:9] == bytes([0x00, 0x28, 0x12, 0x18]), req.hex()
                self.send_l2(4, bytes([0x07, 0x10, 0x00, self.svc_end, 0x00]))
            elif op == 0x08:                                         # read by type (chars)
                start, end = int.from_bytes(req[1:3], "little"), int.from_bytes(req[3:5], "little")
                hs = [h for h in sorted(self.db) if start <= h <= end and self.db[h][0] == 0x2803][:3]
                if not hs:
                    self.send_l2(4, bytes([0x01, 0x08]) + req[1:3] + bytes([0x0A]))
                else:
                    self.send_l2(4, bytes([0x09, 7]) + b"".join(h.to_bytes(2, "little") + self.db[h][1] for h in hs))
            elif op == 0x04:                                         # find information
                start, end = int.from_bytes(req[1:3], "little"), int.from_bytes(req[3:5], "little")
                hs = [h for h in sorted(self.db) if start <= h <= end][:5]
                if not hs:
                    self.send_l2(4, bytes([0x01, 0x04]) + req[1:3] + bytes([0x0A]))
                else:
                    self.send_l2(4, bytes([0x05, 1]) + b"".join(
                        h.to_bytes(2, "little") + self.db[h][0].to_bytes(2, "little") for h in hs))
            elif op in (0x0A, 0x0C):                                 # read, read blob
                h = int.from_bytes(req[1:3], "little")
                off = int.from_bytes(req[3:5], "little") if op == 0x0C else 0
                value = self.db[h][1]
                if off > len(value):
                    self.send_l2(4, bytes([0x01, op]) + req[1:3] + bytes([0x07]))
                else:
                    self.send_l2(4, bytes([op + 1]) + value[off:off + 22])
            elif op == 0x12:                                         # write request
                h = int.from_bytes(req[1:3], "little")
                assert h == 0x17 and req[3:5] == bytes([1, 0]), f"notifications on {h:#x}"
                self.send_l2(4, bytes([0x13]))
                return
            else:
                raise AssertionError(f"unexpected ATT request {req.hex()}")

    def keys(self, *usages, mods=0):
        """A keyboard report (report ID 1: mods, reserved, 6 keys)."""
        rep = bytes([mods, 0]) + bytes(usages) + bytes(6 - len(usages))
        self.send_l2(4, bytes([0x1B, 0x16, 0x00]) + rep)

    def come_back(self):
        """Disconnects, then advertises from a new resolvable private address."""
        self._event(0x05, bytes([0]) + self.LE_HANDLE.to_bytes(2, "little") + bytes([0x08]))
        self._next(want_cmd=0x200B)
        self._next(want_cmd=0x200C)
        prand = bytes([0x11, 0x22, 0x40 | 0x33])
        rpa = self.ah(self.IRK, prand) + prand
        self.advertise(bytes([9, 9, 9, 9, 9, 0x49]), 1, bytes([2, 0x01, 0x04]))    # someone else
        self.advertise(rpa, 1, bytes([2, 0x01, 0x04]))
        self.connect(rpa, 1)
        enc = self._next(want_cmd=0x2019)
        assert enc[12:28] == self.ltk and enc[2:12] == bytes(10), "saved key not used"
        self._event(0x08, bytes([0]) + self.LE_HANDLE.to_bytes(2, "little") + bytes([1]))


def test_bt_keyboard(b, opts):
    """M16 LE keyboard with a simulated MX Keys: 'K' pairs it (LE Secure
    Connections, the code shown on screen typed on the keyboard, its IRK),
    GATT finds the keyboard report in the report map, keys type in the
    monitor (keypad too); it comes back from a private address and is
    recognised by its IRK; the keys are in bm/config.txt. With a USB
    keyboard too, each is a player of its own: USB 1 (white number), the
    LE keyboard 2 (blue), in the bar and in a game."""
    try:
        import Crypto  # noqa: F401  (pycryptodome, for the simulated keyboard)
    except ImportError:
        print("    skipped: pip install pycryptodome")
        return
    tmp = tempfile.mkdtemp(prefix="bm-bt-")
    img = os.path.join(tmp, "sd.img")
    hcd = os.path.join(tmp, "BCM43430A1.hcd")
    with open(hcd, "wb") as f:
        f.write(bytes([0x4C, 0xFC, 4, 1, 2, 3, 4, 0x4E, 0xFC, 4, 0xFF, 0xFF, 0xFF, 0xFF]))
    cart = os.path.join(tmp, "players.bm")
    with open(cart, "wb") as f:
        f.write(mkbm.pack(PLAYERS_CART.encode(), title="AAA players"))
    mksd.build(img, [(hcd, "bm/BCM43430A1.hcd"), (cart, "carts/players.bm")])
    drive = ["-drive", f"if=sd,format=raw,file={img}"]
    q = Qemu(b("kernel.img"), drive + USB_KBD, mini_uart=True)
    q.mini_buf = b""
    try:
        q.boot()
        q.send("K")
        q.expect("(same pins, same speed)\r\n")
        chip = FakeMxKeys(q.port)
        chip.buf, q.buf = q.buf, b""
        chip.init(reset_silent=False)

        early = []

        def passkey():
            early.append(_mini_expect(q, "then Enter:"))
            text = ""
            deadline = time.time() + 10
            import re
            while not re.search(r"\b\d{6}\b", text) and time.time() < deadline:
                q.mini_buf += q.mini.read(0.05)
                text = q.mini_buf.decode(errors="replace")
            m = re.search(r"\b(\d{6})\b", text)
            assert m, text
            return int(m.group(1))
        try:
            chip.pair(FakeBtChip.ADDR, passkey)
            chip.serve_gatt()
        except AssertionError:
            q.mini_buf += q.mini.read(0.5)
            print(q.mini_buf.decode(errors="replace")[-3000:])
            raise
        out = "".join(early) + _mini_expect(q, "ready to type")
        for s_ in ("found keyboard cb:a9:87:65:43:21 MX Keys S (random address)",
                   "pairing, LE Secure Connections, the keyboard types a code",
                   "keyboard 00:6d:9e:12:34:56 paired (Secure Connections, private address)",
                   "keyboard MX Keys S connected"):
            assert s_ in out, out
        # typing: 'i' (info) in the monitor
        time.sleep(0.3)
        chip.keys(0x0C)
        chip.keys()
        _mini_expect(q, "uptime")
        # the menu bar: the USB keyboard is player 1 (white number), the LE
        # keyboard player 2 (blue)
        q.mini.write(b"M")
        _mini_expect(q, "cartridge menu")
        time.sleep(1.0)
        shot_ = q.screendump()
        runs = bar_icons(shot_)
        assert len(runs) == 2 and all(20 <= x1 - x0 <= 27 for x0, x1 in runs), runs
        assert not blue_number(shot_, runs[0]) and blue_number(shot_, runs[1]), runs
        # in a game each keyboard moves its own player
        q.mini.write(b"\r")
        _mini_expect(q, "playing players.bm")
        _mini_expect(q, "players 2 mask 3")
        chip.keys(0x07)                                   # D on the LE keyboard: right
        _mini_expect(q, "player 2 [1 1.0,0.0]")
        chip.keys()
        _mini_expect(q, "player 2 [ 0.0,0.0]")
        sendkeys(q, "a")                                  # A on the USB keyboard: left
        _mini_expect(q, "player 1 [0 -1.0,0.0]")
        q.mini.write(b"q")
        _mini_expect(q, "update+draw")
        time.sleep(0.5)
        q.mini.write(b"q")
        _mini_expect(q, "back to the monitor")
        # back from a private address: found by its IRK, the saved LTK
        chip.come_back()
        chip.serve_gatt()
        _mini_expect(q, "ready to type")
        time.sleep(0.3)
        chip.keys(0x0C)
        chip.keys()
        _mini_expect(q, "uptime")
    finally:
        q.close()
    part = os.path.join(tmp, "part.img")
    with open(img, "rb") as f, open(part, "wb") as o:
        f.seek(2048 * 512)
        o.write(f.read())
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    cfg = subprocess.run(["mtype", "-i", part, "::/BM/CONFIG.TXT"], capture_output=True,
                         text=True, env=env).stdout
    shutil.rmtree(tmp, ignore_errors=True)
    assert "bt_kbd=00:6d:9e:12:34:56 0 " + FakeMxKeys.IRK.hex() in cfg, cfg
    assert "bt_kbd_key=" + chip.ltk.hex() + " 0000 0000000000000000" in cfg, cfg



def test_bt_keyboard_legacy(b, opts):
    """The keyboard without LE Secure Connections: legacy pairing (c1, s1)
    with the code, its LTK/EDIV/Rand saved, the keypad types."""
    try:
        import Crypto  # noqa: F401
    except ImportError:
        print("    skipped: pip install pycryptodome")
        return
    import re
    tmp = tempfile.mkdtemp(prefix="bm-bt-")
    img = os.path.join(tmp, "sd.img")
    hcd = os.path.join(tmp, "BCM43430A1.hcd")
    with open(hcd, "wb") as f:
        f.write(bytes([0x4C, 0xFC, 4, 1, 2, 3, 4, 0x4E, 0xFC, 4, 0xFF, 0xFF, 0xFF, 0xFF]))
    mksd.build(img, [(hcd, "bm/BCM43430A1.hcd")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"], mini_uart=True)
    q.mini_buf = b""
    try:
        q.boot()
        q.send("K")
        q.expect("(same pins, same speed)\r\n")
        chip = FakeMxKeys(q.port)
        chip.buf, q.buf = q.buf, b""
        chip.init(reset_silent=False)
        early = []

        def passkey():
            early.append(_mini_expect(q, "then Enter:"))
            text, deadline = "", time.time() + 10
            while not re.search(r"\b\d{6}\b", text) and time.time() < deadline:
                q.mini_buf += q.mini.read(0.05)
                text = q.mini_buf.decode(errors="replace")
            return int(re.search(r"\b(\d{6})\b", text).group(1))
        chip.pair_legacy(FakeBtChip.ADDR, passkey)
        chip.serve_gatt()
        out = "".join(early) + _mini_expect(q, "ready to type")
        assert "pairing, LE legacy, the keyboard types a code" in out and \
            "keyboard 00:6d:9e:12:34:56 paired (legacy, private address)" in out, out
        # the Lua prompt: 7 * 6 on the keypad, keypad Enter -> 42
        time.sleep(0.3)
        chip.keys(0x0F)                                   # 'l': Lua
        chip.keys()
        _mini_expect(q, "lua>", timeout=10)
        for u in (0x5F, 0x55, 0x5E, 0x58):
            chip.keys(u)
            chip.keys()
        _mini_expect(q, "42")
        _mini_expect(q, "lua> ")
        # alone, the LE keyboard is player 1: one icon, a blue number
        chip.keys(0x29)                                   # Esc: out of Lua
        chip.keys()
        _mini_expect(q, "\n> ")
        q.mini.write(b"M")
        _mini_expect(q, "cartridge menu")
        time.sleep(1.0)
        shot_ = q.screendump()
        runs = bar_icons(shot_)
        assert len(runs) == 1 and blue_number(shot_, runs[0]), runs
    finally:
        q.close()
    part = os.path.join(tmp, "part.img")
    with open(img, "rb") as f, open(part, "wb") as o:
        f.seek(2048 * 512)
        o.write(f.read())
    cfg = subprocess.run(["mtype", "-i", part, "::/BM/CONFIG.TXT"], capture_output=True, text=True,
                         env=dict(os.environ, MTOOLS_SKIP_CHECK="1")).stdout
    shutil.rmtree(tmp, ignore_errors=True)
    assert f"bt_kbd_key={chip.ltk.hex()} 1234 {chip.rand.hex()}" in cfg, cfg


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
    """'P' then 'y' forgets every paired pad: the keys leave bm/config.txt
    (other settings stay)."""
    tmp = tempfile.mkdtemp(prefix="bm-bt-")
    img = os.path.join(tmp, "sd.img")
    cfg = os.path.join(tmp, "config.txt")
    with open(cfg, "w") as f:
        f.write("layout=it\nbt_pad1=1c:66:6d:01:02:03 " + "a0" * 16 + "\n"
                "bt_pad2=1c:66:6d:09:0a:0b " + "c0" * 16 + "\n")
    hcd = os.path.join(tmp, "BCM43430A1.hcd")
    with open(hcd, "wb") as f:
        f.write(bytes([0x4C, 0xFC, 4, 1, 2, 3, 4, 0x4E, 0xFC, 4, 0xFF, 0xFF, 0xFF, 0xFF]))
    mksd.build(img, [(hcd, "bm/BCM43430A1.hcd"), (cfg, "bm/config.txt")])
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
        _mini_expect(q, "bt: 2 devices forgotten")
    finally:
        q.close()
    try:
        part = os.path.join(tmp, "part.img")
        with open(img, "rb") as f, open(part, "wb") as o:
            f.seek(2048 * 512)
            o.write(f.read())
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
        text = subprocess.run(["mtype", "-i", part, "::/BM/CONFIG.TXT"], capture_output=True,
                              text=True, env=env).stdout
        assert "bt_pad" not in text and "layout=it" in text, text
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def test_menu_tabs(b, opts):
    """The tabs with a DS4 (M27): R1 and L1 move between Games, Dev and
    Settings; on Settings its panel opens by itself and Dev is off; B out of
    it goes back to Dev. Up on the first row stays on the covers. PS in the
    menu goes home (Games, panels closed), never to the monitor; PS in the
    monitor opens the games menu."""
    tmp = tempfile.mkdtemp(prefix="bm-tabs-")
    img = os.path.join(tmp, "sd.img")
    hcd = os.path.join(tmp, "BCM43430A1.hcd")
    with open(hcd, "wb") as f:
        f.write(bytes([0x4C, 0xFC, 4, 1, 2, 3, 4, 0x4E, 0xFC, 4, 0xFF, 0xFF, 0xFF, 0xFF]))
    pad, key = FakeDs4Chip.DS4, FakeDs4Chip.KEY
    cfg = os.path.join(tmp, "config.txt")
    with open(cfg, "w") as f:
        f.write(f"layout=it\nbt_pad=1c:66:6d:01:02:03 {key.hex()}\n")
    mksd.build(img, [(hcd, "bm/BCM43430A1.hcd"), (cfg, "bm/config.txt"),
                     (b("demo.bm"), "carts/game.bm")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"], mini_uart=True)
    q.mini_buf = b""

    def press(buttons=0x08, **kw):          # a press and its release
        chip.report(buttons, **kw)
        time.sleep(0.15)
        chip.report(0x08)
        time.sleep(0.4)

    def state(want_tabs, want_text=(), gone=()):
        img_, text = settled_screen(
            q, lambda i, t: tabs_lit(i) == want_tabs and all(any(w in l for l in t) for w in want_text)
            and not any(g in l for l in t for g in gone))
        joined = "\n".join(text)
        assert tabs_lit(img_) == want_tabs, (tabs_lit(img_), joined)
        for w in want_text:
            assert w in joined, f"{w!r} not on the screen:\n{joined}"
        for g in gone:
            assert g not in joined, f"{g!r} still on the screen:\n{joined}"

    try:
        q.expect("(same pins, same speed)\r\n", timeout=30)
        chip = FakeDs4Chip(q.port)
        chip.buf, q.buf = q.buf, b""
        chip.init(reset_silent=False)
        _mini_expect(q, "cartridge menu")
        chip.reconnect(pad, key, 0x0B, (0x50, 0x51), 1)
        _mini_expect(q, "bt: controller 1c:66:6d:01:02:03 connected (player 1)")
        time.sleep(0.5)
        state(["Games"], ["bm native demo"])

        press(shoulders=2)                  # R1: Dev
        state(["Dev"], ["bm SDK"])
        press(shoulders=2)                  # R1: Settings, its panel open, Dev off
        state(["Settings"], ["Controllers", "WiFi and network"])
        if opts.shots:
            _save_png(q.screendump(), os.path.join(opts.shots, "home-tabs-settings.png"))

        # Settings > Controllers > Button icons: the DS4's face buttons of the
        # hints in their colours (the circle of Back red), then white again
        def red_hint():
            img_ = q.screendump()
            return any(r > 220 and g < 140 and b_ < 140 for x in range(640)
                       for y in range(21 * 16, 22 * 16) for r, g, b_ in [pixel(img_, x, y)])
        press(buttons=0x08 | 0x20)          # A (cross): Controllers
        state(["Settings"], ["Settings > Controllers"])
        press(buttons=0x00)                 # up twice: round to Button icons
        press(buttons=0x00)
        state(["Settings"], ["Button icons", "< White >"])
        assert not red_hint(), "white button icons drawn red"
        press(buttons=0x02)                 # right: Colour
        state(["Settings"], ["< Colour >", "button icons: colour"])
        assert red_hint(), "no red circle for Back"
        if opts.shots:
            _save_png(q.screendump(), os.path.join(opts.shots, "home-button-icons.png"))
        press(buttons=0x06)                 # left: White
        state(["Settings"], ["< White >", "button icons: white"])
        assert not red_hint(), "the circle stayed red"
        press(buttons=0x08 | 0x40)          # B: back to Settings
        state(["Settings"], ["Controllers", "WiFi and network"], gone=["Button icons"])
        press(shoulders=2)                  # R1 on the last tab: nothing
        state(["Settings"], ["Controllers"])
        press(buttons=0x08 | 0x40)          # B (circle): out of Settings, back to Dev
        state(["Dev"], ["bm SDK"], gone=["Controllers"])
        press(shoulders=1)                  # L1: Games
        state(["Games"], ["bm native demo"])
        press(shoulders=1)                  # L1 on the first tab: nothing
        state(["Games"], ["bm native demo"])

        # up on the first row stays on the covers: R1 still goes to Dev
        chip.report(0x00)                   # hat up
        time.sleep(0.15)
        chip.report(0x08)
        time.sleep(0.4)
        state(["Games"], ["bm native demo"])

        # PS in the menu: home, not the monitor
        press(shoulders=2)
        press(shoulders=2)
        state(["Settings"], ["Controllers"])
        press(ps=1)
        state(["Games"], ["bm native demo"], gone=["Controllers"])
        time.sleep(0.5)
        assert b"back to the monitor" not in q.mini_buf + q.mini.read(0.2), "PS left the menu"

        # PS in the monitor: the games menu
        q.mini.write(b"q")
        _mini_expect(q, "back to the monitor")
        _mini_expect(q, "> ")
        press(ps=1)
        _mini_expect(q, "cartridge menu")
        time.sleep(0.5)
        state(["Games"], ["bm native demo"])
        q.mini.write(b"q")
        _mini_expect(q, "back to the monitor")
    finally:
        q.close()
        shutil.rmtree(tmp, ignore_errors=True)


def test_bt_two_pads(b, opts):
    """M16: two DS4 paired earlier (the first by an older kernel, as
    'bt_pad') come back together and light up in their players' colours; a
    third one pairs as player 3 (the old key becomes bt_pad1). In a game each
    pad drives its own player (btn(i, p), stick(p)); the serial keys are the
    first player without a pad."""
    tmp = tempfile.mkdtemp(prefix="bm-bt-")
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
        f.write(mkbm.pack(PLAYERS_CART.encode(), title="AAA players"))
    mksd.build(img, [(hcd, "bm/BCM43430A1.hcd"), (cfg, "bm/config.txt"),
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
        text = subprocess.run(["mtype", "-i", part, "::/BM/CONFIG.TXT"], capture_output=True,
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
    tmp = tempfile.mkdtemp(prefix="bm-sd-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [(b("demo.bm"), "carts/game.bm")], 4096)
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"] + USB_KBD)
    try:
        out = q.expect("cartridge menu", timeout=90).decode(errors="replace")
        assert "sd: SDHC card (sdhost), FAT32, 4095 MiB, label BMSD; 1 cartridges" in out, out
        time.sleep(1.0)
        shot_ = q.screendump()                # the bar: the keyboard icon (M27)
        runs = bar_icons(shot_)
        assert len(runs) == 1 and 20 <= runs[0][1] - runs[0][0] <= 27, runs
        assert not blue_number(shot_, runs[0]), "USB: a white number"
        # the hints: player 1's keyboard, Enter Play, C Options, Esc Monitor
        widths = [x1 - x0 for x0, x1 in prompt_spans(shot_, 21)]
        assert len(widths) == 3 and widths[0] >= 34 and widths[1] <= 16 and 20 <= widths[2] <= 28, widths
        sendkeys(q, "e")                      # E is R1: the Dev tab
        img_, text = settled_screen(q, lambda i, t: tabs_lit(i) == ["Dev"])
        assert tabs_lit(img_) == ["Dev"], "\n".join(text)
        sendkeys(q, "q")                      # Q is L1: back to Games
        img_, text = settled_screen(q, lambda i, t: tabs_lit(i) == ["Games"])
        assert tabs_lit(img_) == ["Games"], "\n".join(text)
        sendkeys(q, "c")                      # C is the X button: the options (M27)
        # (with five tools the info rows, Author..., are below: the panel scrolls)
        opts_row = "Open in bm Mesh"
        _, text = settled_screen(q, lambda i, t: any(opts_row in l for l in t))
        assert any(opts_row in l for l in text) and any("Play" in l for l in text), "\n".join(text)
        sendkeys(q, "x")                      # X is the B button: back
        _, text = settled_screen(q, lambda i, t: not any(opts_row in l for l in t))
        assert not any(opts_row in l for l in text), "\n".join(text)
        sendkeys(q, "ret")
        q.expect("playing game.bm", timeout=10)
        time.sleep(1.5)
        sendkeys(q, "esc")
        q.expect("update+draw", timeout=15)
        time.sleep(0.5)
        sendkeys(q, "ctrl-esc")               # the menu: Ctrl+Esc to the monitor
        q.expect("back to the monitor", timeout=10)
        q.expect("> ")
    finally:
        q.close()
        os.remove(img)


def test_menu_scroll(b, opts):
    """Three rows of covers and two on screen: the scroll bar at the right
    says where they are, so the row scrolled out at the top (Astro Wing and
    Chaos Kitchen on the Pi) is not taken for gone."""
    tmp = tempfile.mkdtemp(prefix="bm-scroll-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [(b("demo.bm"), f"carts/g{i}.bm") for i in range(1, 10)])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])

    def thumb():
        time.sleep(1.0)                         # the grid eases to its place
        t = None
        for _ in range(8):                      # a whole frame: two reads agree
            a, b_ = scroll_thumb(q.screendump()), scroll_thumb(q.screendump())
            if a == b_:
                t = a
                break
        assert t is not None, "no scroll bar"
        return t

    try:
        q.expect(MENU, timeout=30)
        top = thumb()
        assert top[0] <= 114 and 100 <= top[1] - top[0] <= 130, top    # 2 of 3 rows
        keys = "ss"                             # the third row: the first scrolls out
        for k in keys:
            q.send(k)
            time.sleep(0.25)
        low = thumb()
        assert low[1] >= 286 and low[1] - low[0] == top[1] - top[0], (top, low)
        for k in "ww":
            q.send(k)
            time.sleep(0.25)
        assert thumb() == top
        q.send("q")
        q.expect("back to the monitor", timeout=10)
    finally:
        q.close()
        shutil.rmtree(tmp, ignore_errors=True)


def test_usb_hid_gamepad(b, opts):
    """Generic HID parser: QEMU's usb-tablet (report descriptor with 3
    buttons and absolute X/Y) is taken as a gamepad; button 1 = A."""
    tmp = tempfile.mkdtemp(prefix="bm-tablet-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [(b("demo.bm"), "carts/demo.bm")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}",
                               "-device", "usb-tablet,port=1"])
    try:
        out = q.expect("cartridge menu", timeout=90).decode(errors="replace")
        assert "usb: gamepad 0627:0001 'QEMU USB Tablet'" in out, out
        q.monitor("mouse_move 16384 16384")    # centre: stick released
        # the tablet's button report also moves up once: on the first row
        # that does nothing (the tabs are L1 / R1), A plays the cover
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
        shutil.rmtree(tmp, ignore_errors=True)


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


def test_village(b, opts):
    """Studio Village: the 3D models made with bm Studio (carts/village/
    models.glb, packed by make with their sprite sheet) are drawn by the
    console: grass, roof tiles and sky on screen, no Lua error."""
    q = Qemu(b("kernel.img"))
    try:
        q.expect(MENU, timeout=30)
        time.sleep(0.5)
        with open(b("carts/village.bm"), "rb") as f:
            assert _upload(q, f.read())
        time.sleep(3)
        for _ in range(10):
            img = q.screendump()
            w, h, px = img
            cols = [tuple(px[(y * w + x) * 3:(y * w + x) * 3 + 3])
                    for y in range(0, h, 4) for x in range(0, w, 4)]
            grass = sum(g > 60 and g > r + 20 and g > b + 20 for r, g, b in cols)
            roof = sum(r > 70 and r > 2 * g and r > 2 * b for r, g, b in cols)
            sky = sum(b > 150 and b > r + 40 for r, g, b in cols)
            if grass > 300 and roof > 20 and sky > 300:
                break
            time.sleep(0.5)
        if opts.shots:
            _save_png(img, os.path.join(opts.shots, "village.png"))
        print(f"     village colours: grass {grass}, roof {roof}, sky {sky}, {len(set(cols))} distinct")
        assert grass > 300 and roof > 20 and sky > 300, (grass, roof, sky)
        assert len(set(cols)) > 100, len(set(cols))     # textures, not flat faces
        q.send("x")                             # B: night
        time.sleep(1)
        q.send("q")
        out = q.expect("update+draw", timeout=10).decode(errors="replace")
        assert "stopped with an error" not in out, out
        assert '"Studio Village"' in out, out[-300:]
    finally:
        q.close()


def test_studio_cart(b, opts):
    """A cartridge written by bm Studio (make test-studio: tests/studio/
    test_core.js) plays on the console: the model viewer that a new project
    gets as its code shows the models (bounds3d, model, models), with the
    sections the kernel does not know left alone."""
    path = b("studio-test.bm")
    if not os.path.exists(path):
        print("     skipped: no build/studio-test.bm (make test-studio needs Node)")
        return
    q = Qemu(b("kernel.img"))
    try:
        q.expect(MENU, timeout=30)
        time.sleep(0.5)
        # first the one with a skeleton: the viewer plays its animations (X: the next one)
        with open(b("studio-test-anim.bm"), "rb") as f:
            assert _upload(q, f.read())
        time.sleep(1.5)
        q.send("c")
        time.sleep(0.5)
        q.send("q")
        out = q.expect("update+draw", timeout=10).decode(errors="replace")
        assert "stopped with an error" not in out, out
        time.sleep(0.5)
        with open(path, "rb") as f:
            assert _upload(q, f.read())
        time.sleep(2.5)
        for _ in range(10):
            w, h, px = q.screendump()
            cols = [tuple(px[(y * w + x) * 3:(y * w + x) * 3 + 3]) for y in range(0, h, 2) for x in range(0, w, 2)]
            bg = sum(abs(r - 0x1C) < 12 and abs(g - 0x20) < 12 and abs(b_ - 0x30) < 12 for r, g, b_ in cols)
            grass = sum(g > 70 and g > r + 15 and g > b_ + 15 for r, g, b_ in cols)
            if grass > 50 and bg > len(cols) // 3:
                break
            time.sleep(0.5)
        print(f"     studio cart: background {bg}, grass {grass} of {len(cols)}")
        assert grass > 50 and bg > len(cols) // 3, (grass, bg)
        q.send("d")                             # the next model
        time.sleep(0.5)
        q.send("q")
        out = q.expect("update+draw", timeout=10).decode(errors="replace")
        assert "stopped with an error" not in out, out
    finally:
        q.close()


def test_studio3d(b, opts):
    """The 3D studio on the console (Dev tab; a game's options, "Open in the
    3D studio"): the player shows the models and animations of the village;
    a new project gets a block, is saved, tried (its viewer plays) and the
    studio comes back to it; the file holds the model the kernel reads."""
    tmp = tempfile.mkdtemp(prefix="bm-s3d-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [(b("carts/village.bm"), "carts/village.bm")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])

    def keys(*ks, gap=0.3):
        for k in ks:
            q.send(k)
            time.sleep(gap)

    def screen(want, tries=40):
        for _ in range(tries):
            _, text = settled_screen(q, lambda i, t: all(any(w in l for l in t) for w in want), tries=2)
            if all(any(w in l for l in text) for w in want):
                return "\n".join(text)
            time.sleep(0.25)
        raise AssertionError(f"not on the screen: {want}\n" + "\n".join(text))

    def shot(name):
        if opts.shots:
            _save_png(q.screendump(), os.path.join(opts.shots, f"studio3d-{name}.png"))

    F2, F3, F4, UP, DOWN, ESC = "\x1bOQ", "\x1bOR", "\x1bOS", "\x1b[A", "\x1b[B", "\x1b"
    try:
        q.expect(MENU, timeout=30)
        time.sleep(0.5)
        screen(["Games", "Studio Village"])
        keys("x")
        screen(["Open in the SDK", "Open in bm Code", "Open in the Sound editor", "Open in the 3D studio"])
        keys("s", "s", "s", "s", "\r")
        text = screen(["play", "animate", "MODELS", "ground", "villager", "169 vertices, 288 triangles"])
        assert "opened /carts/village.bm" in text, text
        for _ in range(7):
            keys(DOWN)
        screen(["ANIMATIONS", "idle", "walk", "wave", "112 vertices, 168 triangles, 7 bones"])
        keys("k")
        shot("player")
        keys(F3)
        screen(["BONES", "hips", "spine", "arm.L", "leg.R", "tail of hips"])
        shot("rig")
        keys(F4)
        screen(["BONES", "idle  1/3  smooth  loop  2.00 s"])
        shot("animate")

        # a new project: a block, saved as CUBE.BM
        keys(ESC, gap=0.6)
        screen(["bm 3D studio", "New project", "Exit 3D studio"])
        keys(DOWN, DOWN, "\r", gap=0.4)         # Continue, Open..., New project
        screen(["BLOCK", "cell 0,0,0", "model: 0 faces"])
        keys(" ")
        screen(["model: 6 faces"])
        shot("build")
        # a skeleton (root and a child) and an animation with a turn at 0.25 s:
        # the kernel checks each new ANIM section (cart_data) before drawing it
        keys(F3, "n", "n", gap=0.5)
        screen(["BONES", "root", "bone2", "tail of bone2"])
        keys(F4, "n", gap=0.5)
        screen(["anim1  1/1  smooth  loop  1.00 s"])
        keys("\x1b[C", "\x1b[C", "\x1b[C", "w", gap=0.4)
        screen(["TURN root   0.25 s  frame 3  key"])
        shot("keyframe")
        keys(ESC, gap=0.6)
        for _ in range(4):
            keys(DOWN)                          # down to "Save as..."
        keys("\r")
        screen(["file name"])
        for _ in range(8):
            keys("\x7f", gap=0.1)
        for ch in "CUBE\r":
            keys(ch, gap=0.1)
        screen(["saved /carts/CUBE.BM"])

        # try it: the viewer of a new project plays it, then the studio comes back
        keys("\x1b[15~", gap=1)                 # F5
        time.sleep(3)
        keys("q")                               # the game ends (its keys are a gamepad's)
        out = q.expect('bm: "New 3D project"', timeout=20).decode(errors="replace")
        assert "stopped with an error" not in out, out
        screen(["back from the game", "anim1  1/1"])     # the page it was on

        # out of the studio: back to the menu
        keys(ESC, gap=0.6)
        keys(UP, "\r")                          # up from Continue: Exit 3D studio
        screen(["Games", "last: 3D studio on village.bm"])
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
        saved = subprocess.run(["mtype", "-i", part, "::/CARTS/CUBE.BM"], capture_output=True, env=env).stdout
        secs = dict(bmmesh.cart_sections(saved))
        models, _ = bmmesh.decode(secs[bmmesh.SEC_MESH])
        assert [m["name"] for m in models] == ["model"] and len(models[0]["faces"]) == 12, models
        assert len(models[0]["verts"]) == 8, models[0]["verts"]
        anim = secs[bmmesh.SEC_ANIM]            # ANIM: one rig, 2 bones, 1 clip of 2 keys
        nb, nc, nv = struct.unpack_from("<HHH", anim, 8 + 16)
        clip = 8 + 24 + nb * 44 + ((nv + 3) & ~3)
        assert struct.unpack_from("<H", anim)[0] == 1 and (nb, nc, nv) == (2, 1, 8), (nb, nc, nv)
        assert anim[clip:clip + 5] == b"anim1" and struct.unpack_from("<H", anim, clip + 16)[0] == 2
        assert saved[24:38] == b"New 3D project", saved[24:48]
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def test_mesh(b, opts):
    """bm Mesh on the console (a game's options, "Open in bm Mesh"): it lists
    the meshes Astro Wing builds in its code (cart_meshes runs the code
    apart), copies the ship as a model and moves its vertices, copies it as
    code (mesh_ship() at the end of main.lua) and saves: the file holds the
    model and the code the kernel and the build read."""
    tmp = tempfile.mkdtemp(prefix="bm-mesh-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [(b("carts/astrowing.bm"), "carts/astrowing.bm")])
    with open(b("carts/astrowing.bm"), "rb") as f:
        lua0 = dict(bmmesh.cart_sections(f.read()))[1]
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])

    def keys(*ks, gap=0.3):
        for k in ks:
            q.send(k)
            time.sleep(gap)

    def screen(want, tries=40):
        for _ in range(tries):
            _, text = settled_screen(q, lambda i, t: all(any(w in l for l in t) for w in want), tries=2)
            if all(any(w in l for l in text) for w in want):
                return "\n".join(text)
            time.sleep(0.25)
        raise AssertionError(f"not on the screen: {want}\n" + "\n".join(text))

    def shot(name):
        if opts.shots:
            _save_png(q.screendump(), os.path.join(opts.shots, f"mesh-{name}.png"))

    F1, F2, UP, DOWN, ESC, SAVE = "\x1bOP", "\x1bOQ", "\x1b[A", "\x1b[B", "\x1b", "\x13"
    try:
        q.expect(MENU, timeout=30)
        time.sleep(0.5)
        screen(["Games", "Astro Wing"])
        keys("x")
        screen(["Open in the 3D studio", "Open in bm Mesh"])
        keys("s", "s", "s", "s", "s", "\r")
        text = screen(["list", "edit", "MESHES 13", "ship", "30 vertices, 32 triangles", "built by the game's code"])
        assert "0 models, 0 code meshes, 13 from the game's code" in text, text
        assert "core_hot" in text and "turret" in text, text
        shot("list")

        # mesh -> model, then its vertices 0.2 up
        keys("m", gap=0.5)
        screen(["copied as the model ship"])
        keys(F2, gap=0.5)
        screen(["VERTICES: 0 chosen", "model (MESH section)"])
        keys("a", "g", UP, UP)
        screen(["VERTICES: 30 chosen", "MOVE", "y 0.2"])
        shot("edit")
        keys("\r", SAVE, gap=0.6)
        screen(["saved /carts/astrowing.bm"])

        # mesh -> code
        keys(F1, DOWN, gap=0.5)
        screen(["built by the game's code", "read only"])
        keys("c", gap=0.5)
        screen(["copied as code: mesh_ship()"])
        keys(SAVE, gap=0.6)
        screen(["saved /carts/astrowing.bm"])
        shot("code")

        # out of bm Mesh: back to the menu
        keys(ESC, gap=0.6)
        screen(["bm Mesh", "Exit bm Mesh"])
        keys(UP, "\r")                          # up from Continue: Exit bm Mesh
        screen(["Games", "last: bm Mesh on astrowing.bm"])
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
        saved = subprocess.run(["mtype", "-i", part, "::/CARTS/ASTROWING.BM"], capture_output=True, env=env).stdout
        secs = dict(bmmesh.cart_sections(saved))
        models, _ = bmmesh.decode(secs[bmmesh.SEC_MESH])
        assert [m["name"] for m in models] == ["ship"], models
        ship = models[0]
        assert len(ship["verts"]) == 30 and len(ship["faces"]) == 32, (len(ship["verts"]), len(ship["faces"]))
        # the nose of the fighter, (0, 0, 2.2) in the game's code, is 0.2 higher
        assert any(abs(v[0]) < 1e-6 and abs(v[1] - 0.2) < 1e-5 and abs(v[2] - 2.2) < 1e-5 for v in ship["verts"]), ship
        lua = secs[1]
        assert lua.startswith(lua0) and b"function mesh_ship()" in lua and b"-- [bm Mesh end]" in lua, lua[-400:]
        assert bmmesh.SEC_ANIM not in secs
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def _sheet_pixels(data):
    """The sheet of a .bm: (w, h, [0xRRGGBB or None ...]), from SHEET8 or
    SHEET, and the SHEET8 palette (None for SHEET)."""
    secs = dict(bmmesh.cart_sections(data))
    if 5 in secs:
        s = secs[5]
        w, h, nc = struct.unpack_from("<HHH", s)
        pal = [(s[8 + i * 4] << 16 | s[9 + i * 4] << 8 | s[10 + i * 4]) if s[11 + i * 4] >= 128 else None
               for i in range(nc)]
        px, q = [], 8 + nc * 4
        while len(px) < w * h:
            t = s[q]
            q += 1
            if t < 128:
                px += [pal[i] for i in s[q:q + t + 1]]
                q += t + 1
            else:
                px += [pal[s[q]]] * (t - 126)
                q += 1
        assert q == len(s) and len(px) == w * h, "SHEET8: the runs do not add up"
        return w, h, px, pal
    s = secs[2]
    w, h = struct.unpack_from("<HH", s)
    px = [(s[4 + i * 4] << 16 | s[5 + i * 4] << 8 | s[6 + i * 4]) if s[7 + i * 4] >= 128 else None
          for i in range(w * h)]
    return w, h, px, None


def test_pixel(b, opts):
    """bm Pixel on the console (a game's options, "Open in bm Pixel"): the
    village's sheet with its palette (SHEET8), a pixel and a line drawn
    with the palette's first colour, saved: in the file only those pixels
    change (the others keep their 24 bits), the palette comes first in the
    SHEET8, the code, cover, models and skeletons stay byte for byte."""
    tmp = tempfile.mkdtemp(prefix="bm-pixel-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [(b("carts/village.bm"), "carts/village.bm")])
    with open(b("carts/village.bm"), "rb") as f:
        village0 = f.read()
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])

    def keys(*ks, gap=0.3):
        for k in ks:
            q.send(k)
            time.sleep(gap)

    def screen(want, tries=40):
        for _ in range(tries):
            _, text = settled_screen(q, lambda i, t: all(any(w in l for l in t) for w in want), tries=2)
            if all(any(w in l for l in text) for w in want):
                return "\n".join(text)
            time.sleep(0.25)
        raise AssertionError(f"not on the screen: {want}\n" + "\n".join(text))

    def shot(name):
        if opts.shots:
            _save_png(q.screendump(), os.path.join(opts.shots, f"pixel-{name}.png"))

    F2, F3, UP, RIGHT, ESC, SAVE = "\x1bOQ", "\x1bOR", "\x1b[A", "\x1b[C", "\x1b", "\x13"
    try:
        q.expect(MENU, timeout=30)
        time.sleep(0.5)
        screen(["Games", "Studio Village"])
        keys("x")
        screen(["Open in bm Mesh"])
        for _ in range(6):
            keys("s")                           # Play, SDK, Code, Sound, 3D studio, Mesh, Pixel
        screen(["Open in bm Pixel"])
        keys("\r", gap=1.0)
        text = screen(["draw", "sheet", "palette", "COLOURS", "sprite 0  (0,0)  16x16", "spr(0, x, y, 2, 2)"])
        assert "(the file's palette)" in text, text
        shot("draw")
        keys("1", "b", " ", "l", " ", RIGHT, RIGHT, RIGHT, " ")
        screen(["line"])
        keys(F2, gap=0.5)
        screen(["sheet 256x256", "sprite 0 at (0,0)"])
        shot("sheet")
        keys(F3, gap=0.5)
        screen(["PALETTE", "edit", "add", "remove"])
        shot("palette")
        keys("\r", SAVE, gap=0.6)
        screen(["saved /carts/village.bm"])
        keys(ESC, gap=0.6)
        screen(["bm Pixel", "Exit bm Pixel"])
        keys(UP, "\r")                          # up from Continue: Exit bm Pixel
        screen(["Games", "last: bm Pixel on village.bm"])
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
        saved = subprocess.run(["mtype", "-i", part, "::/CARTS/VILLAGE.BM"], capture_output=True, env=env).stdout
        secs, secs0 = dict(bmmesh.cart_sections(saved)), dict(bmmesh.cart_sections(village0))
        for t in (1, 4, 8, 9):
            assert secs[t] == secs0[t], f"section {t} changed"
        assert 5 in secs and 2 not in secs, sorted(secs)
        w, h, px, pal = _sheet_pixels(saved)
        w0, h0, px0, pal0 = _sheet_pixels(village0)
        assert (w, h) == (w0, h0), (w, h)
        first = next(c for c in pal0 if c is not None)
        drawn = {(0, 0), (1, 0), (2, 0), (3, 0)}       # the pointer starts at the sprite's corner
        for (x, y) in drawn:
            assert px[y * w + x] == first, (x, y, hex(px[y * w + x] or 0), hex(first))
        other = [i for i in range(w * h) if (i % w, i // w) not in drawn and px[i] != px0[i]]
        assert not other, f"{len(other)} pixels not drawn on changed, e.g. {other[:5]}"
        opaque0 = [c for c in pal0 if c is not None]
        assert [c for c in pal if c is not None][:len(opaque0)] == opaque0, "the palette first, as it was"
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def test_pixel_big(b, opts):
    """bm Pixel on the biggest sheet of the games, Titan Clash's (2048x3448,
    SHEET8): it opens zoomed out, a pixel drawn and saved (a frame says
    "saving" while the file is written); in the file only that pixel
    changes and the rest stays byte for byte."""
    tmp = tempfile.mkdtemp(prefix="bm-pixelbig-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [(b("carts/titan.bm"), "carts/titan.bm")])
    with open(b("carts/titan.bm"), "rb") as f:
        titan0 = f.read()
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])

    def keys(*ks, gap=0.3):
        for k in ks:
            q.send(k)
            time.sleep(gap)

    def screen(want, tries=120):
        for _ in range(tries):
            _, text = settled_screen(q, lambda i, t: all(any(w in l for l in t) for w in want), tries=2)
            if all(any(w in l for l in text) for w in want):
                return "\n".join(text)
            time.sleep(0.25)
        raise AssertionError(f"not on the screen: {want}\n" + "\n".join(text))

    def shot(name):
        if opts.shots:
            _save_png(q.screendump(), os.path.join(opts.shots, f"pixel-big-{name}.png"))

    F1, F2, UP, ESC, SAVE = "\x1bOP", "\x1bOQ", "\x1b[A", "\x1b", "\x13"
    try:
        q.expect(MENU, timeout=30)
        time.sleep(0.5)
        screen(["Games", "Titan Clash"])
        keys("x")
        screen(["Open in bm Mesh"])
        for _ in range(6):
            keys("s")                           # down to Open in bm Pixel
        screen(["Open in bm Pixel"])
        keys("\r", gap=1.0)
        screen(["COLOURS 165", "sheet 2048x3448, 165 colours (the file's palette)"])
        keys(F2, gap=0.5)
        screen(["sheet 2048x3448  zoom 1/4"])
        shot("sheet")
        keys(F1, gap=0.5)
        screen(["COLOURS 165"])
        keys("1", "b", " ")
        screen(["/carts/titan.bm*"])
        keys(SAVE, gap=0.1)
        screen(["saving /carts/titan.bm ..."])
        shot("saving")
        screen(["saved /carts/titan.bm"])
        keys(ESC, gap=0.6)
        screen(["bm Pixel", "Exit bm Pixel"])
        keys(UP, "\r")
        screen(["Games", "last: bm Pixel on titan.bm"])
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
        saved = subprocess.run(["mtype", "-i", part, "::/CARTS/TITAN.BM"], capture_output=True, env=env).stdout
        secs, secs0 = dict(bmmesh.cart_sections(saved)), dict(bmmesh.cart_sections(titan0))
        assert sorted(secs) == sorted(secs0), (sorted(secs), sorted(secs0))
        for t in secs0:
            if t != 5:
                assert secs[t] == secs0[t], f"section {t} changed"
        w, h, px, pal = _sheet_pixels(saved)
        w0, h0, px0, pal0 = _sheet_pixels(titan0)
        assert (w, h) == (2048, 3448) == (w0, h0), (w, h)
        assert pal == pal0, "the palette as it was"
        changed = [i for i in range(w * h) if px[i] != px0[i]]
        first = next(c for c in pal0 if c is not None)
        assert changed == [0] and px[0] == first, [(i % w, i // w, px0[i], px[i]) for i in changed[:5]]
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


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
        keys("sss ")                           # QUIT (after RESUME, VOLUME, RESTART): the map
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
    loader = bm_load.Loader(q.port, echo=q)
    return loader.upload(data)


def _bm(src):
    return mkbm.pack(src.encode(), title="test cart")


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
  log("nobank", sfx(0), music())
  quit()
end
"""

# a cartridge with the demo bank of the Sound editor (carts/sound/demo.json)
BANK_CART = r"""
local n = 0
function _init()
  log("hz", hz(69), hz("A4"), math.floor(hz("C4") * 100), hz("Bb3") < hz("B3"))
  note(1, "C4", 0, SINE, 100)
  log("frac", apu(1, 0) + apu(1, 1) * 256, apu(1, 10), apu(1, 2), SINE, METAL)
  log("vol", volume(), volume(4), volume(12), volume(-3))
  volume(10)
  local v = sfx(0)                       -- COIN, no music: the highest voice
  log("sfx", v, sfxpos(v))
  music(0)
end
function _update()
  n = n + 1
  if n == 90 then                        -- 1.5 s at 112 BPM: about 11 steps
    local s, pos, step, pat = music()
    log("music", s, pos, step, pat)
    log("free", sfx(3))                  -- a voice the song leaves free: 6 or 7
    tempo(2)
    mute(2)
    arp(1, "major", 40)
    vibrato(1, 0.5, 6)
    slide(1, 880, 100)
    music(-1, 200)
  end
  if n == 130 then
    log("stopped", music() == nil, sfx(99))
    log("bad", select(2, pcall(sfx, 0, 9)), select(2, pcall(hz, "H2")), select(2, pcall(arp, 0, "jazz")))
    quit()
  end
end
"""


def _bank_bm(src, **kw):
    sys.path.insert(0, os.path.join(HERE, "..", "scripts"))
    import bmaudio
    return mkbm.pack(src.encode(), audio=bmaudio.load(os.path.join(HERE, "..", "carts", "sound", "demo.json")), **kw)


def test_audio_bank(b, opts):
    """Sound banks: a cartridge's AUDIO section is played by sfx() and
    music() (the player moves even without HDMI audio), note names and
    fractions of a hertz, the master volume, the helpers."""
    q = Qemu(b("kernel.img"))
    try:
        q.boot()
        assert _upload(q, _bank_bm(BANK_CART, title="bank test"))
        out = q.expect("bad\t", timeout=20).decode(errors="replace")
        out += q.expect("\n").decode(errors="replace")
        assert "sound bank not loaded" not in out, out
        assert "hz\t440.0\t440.0\t26162\ttrue" in out, out
        assert "frac\t261\t160\t4\t4\t5" in out, out
        assert "vol\t10\t4\t10\t0" in out, out
        assert "sfx\t7\t0\t0" in out, out
        m = re.search(r"music\t0\t0\t(\d+)\t0", out)
        assert m and 5 <= int(m.group(1)) <= 15, out
        assert re.search(r"free\t[67]\r?\n", out), out
        assert "stopped\ttrue\tnil" in out, out
        assert "voice 0..7" in out and "a note name" in out and "invalid option 'jazz'" in out, out
        q.expect("> ", timeout=10)
    finally:
        q.close()


VOLUME_CART = r"""
function _init() log("volume", volume(), volume(3)) quit() end
"""


def test_volume_saved(b, opts):
    """The volume a game sets (its pause menu) is kept in bm/config.txt and
    comes back after a reboot."""
    tmp = tempfile.mkdtemp(prefix="bm-vol-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [(b("carts/pong.bm"), "carts/pong.bm")])
    drive = ["-drive", f"if=sd,format=raw,file={img}"]
    try:
        q = Qemu(b("kernel.img"), drive)
        try:
            q.boot()
            assert _upload(q, mkbm.pack(VOLUME_CART.encode(), title="volume test"))
            out = q.expect("volume\t", timeout=15).decode(errors="replace")
            out += q.expect("\n").decode(errors="replace")
            assert out.rstrip().endswith("10\t3"), out
            q.expect("> ", timeout=10)
        finally:
            q.close()
        q = Qemu(b("kernel.img"), drive)
        try:
            out = q.boot().decode(errors="replace")
            assert "volume 3/10" in out, out
        finally:
            q.close()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def _sd_files(img):
    """the FAT partition of an image, for mtools and fsck"""
    part = img + ".part"
    with open(img, "rb") as f, open(part, "wb") as o:
        f.seek(2048 * 512)
        o.write(f.read())
    return part


def test_sound_editor(b, opts):
    """The Sound editor (Dev tab, monitor A): starts on the demo, types a
    note with the piano keys, saves the demo as a new sound pack, opens a
    game from the SD card, gives it a pattern and saves it into the game;
    the game then plays with its new bank, and the card is a clean FAT32
    volume."""
    tmp = tempfile.mkdtemp(prefix="bm-snd-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [(b("carts/pong.bm"), "carts/pong.bm")])
    drive = ["-drive", f"if=sd,format=raw,file={img}"]
    q = Qemu(b("kernel.img"), drive)

    def k(s, gap=0.3):
        q.send(s)
        time.sleep(gap)
    try:
        q.boot()
        k("A", 3)                                           # the editor, on the demo
        k("\x1bOR")                                         # F3: pattern
        for _ in range(5):
            k("\x1b[B")                                     # track 6: empty in pattern 0
        k("q")                                              # C5
        k("\x13", 1)                                        # Ctrl+S: a new pack, named
        k("\r")                                             # "DEMO"
        q.expect("sound: saved /bm/sounds/DEMO.BM", timeout=20)
        k("\x0f", 1.5)                                      # Ctrl+O: the files
        k("\r")                                             # /carts/pong.bm
        q.expect("sound: opened /carts/pong.bm (no sounds yet)", timeout=20)
        k("\x1bOR")
        k("z")                                              # C4 on track 1, step 1
        k("\x13", 1)
        q.expect("sound: saved /carts/pong.bm", timeout=20)
        k("\x1b", 0.6)                                      # the menu
        k("\x1b[6~")
        k("\x1b[6~")                                        # the last item: Exit
        k("\r")
        q.expect("> ", timeout=15)
    finally:
        q.close()
    try:
        part = _sd_files(img)
        fsck = subprocess.run(["fsck.vfat", "-n", part], capture_output=True, text=True)
        assert fsck.returncode == 0, fsck.stdout + fsck.stderr
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
        sys.path.insert(0, os.path.join(HERE, "..", "scripts"))
        import bmaudio
        pong = subprocess.run(["mtype", "-i", part, "::/CARTS/PONG.BM"], capture_output=True, env=env).stdout
        assert pong[:8] == b"BMCART\x00\x00", pong[:16]
        import zlib
        assert zlib.crc32(pong[128:]) & 0xFFFFFFFF == int.from_bytes(pong[20:24], "little"), "pong.bm CRC"
        bank = bmaudio.unpack(bmaudio.extract(pong))
        assert bank["patterns"][0]["tracks"]["0"][0].startswith("C4"), bank["patterns"]
        assert b"function _update" in pong                  # its code is still there
        pack = subprocess.run(["mtype", "-i", part, "::/BM/SOUNDS/DEMO.BM"], capture_output=True, env=env).stdout
        demo = bmaudio.unpack(bmaudio.extract(pack))
        assert demo["patterns"][0]["tracks"]["5"][0].startswith("C5"), demo["patterns"][0]
        assert len(demo["songs"]) == 2 and demo["songs"][0]["name"] == "DEMO", demo["songs"]
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    # the game with its bank runs as before
    q = Qemu(b("kernel.img"))
    try:
        q.boot()
        assert _upload(q, pong)
        time.sleep(2)
        q.send("q")
        out = q.expect("> ", timeout=15).decode(errors="replace")
        assert "sound bank not loaded" not in out and "stopped with an error" not in out, out
    finally:
        q.close()


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
        assert _upload(q, mkbm.pack(AUDIO_CART.encode(), title="audio test"))
        out = q.expect("bad\t", timeout=15).decode(errors="replace")
        out += q.expect("\n").decode(errors="replace")
        assert "apu\t440\t0\t64\t1200\t3\t60\t77\t1" in out, out
        assert "off\t0\tboolean\t1\t2" in out, out
        assert "bad\t" in out and "voice 0..7" in out and "register 0..15" in out, out
        out = q.expect("nobank\t", timeout=5).decode(errors="replace")
        out += q.expect("\n").decode(errors="replace")
        assert "nobank\tnil\tnil" in out, out
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
        assert _upload(q, mkbm.pack(TEX_CART.encode(), title="texture test"))
        out = q.expect("tex\t", timeout=15).decode(errors="replace")
        out += q.expect("\n").decode(errors="replace")
        assert "ff0000 0000ff\t2" in out, out
        q.expect("> ", timeout=10)
    finally:
        q.close()


MODEL_CART = r"""
local tile, gem
function _init()
  for y = 0, 15 do for x = 0, 15 do sset(x, y, x < 8 and 0xFF0000 or 0x0000FF) end end
  local names = models()
  log("models", #names, names[1], names[2], tostring(model("nope")), tostring(model(3)))
  tile, gem = model("tile"), model(2)
  log("bounds", bounds3d(gem))
end
local n = 0
function _update() n = n + 1 end
function _draw()
  cls(0)
  zclear()
  camera3d(0, 0, -3)
  light3d(0, 0, -1, 1)
  if n < 4 then
    draw3d(tile, 0, 0, 0)
    if n == 3 then log("drawn tile", string.format("%06x %06x", pget(280, 180), pget(360, 180)), stat(4)) end
  else
    draw3d(gem, 0, 0, 0)
    log("drawn gem", string.format("%06x %06x", pget(320, 170), pget(40, 30)), stat(4))
    quit()
  end
end
"""


def test_models(b, opts):
    """bm Studio: model() builds meshes from the MESH section of the
    cartridge, textured with the sprite sheet or in plain colours."""
    T = bmmesh.TEXTURED
    mesh = bmmesh.encode([
        {"name": "tile", "verts": [(-1, -1, 0), (1, -1, 0), (1, 1, 0), (-1, 1, 0)],
         "faces": [(0, 2, 1, T, (0, 16, 16, 0, 16, 16)), (0, 3, 2, T, (0, 16, 0, 0, 16, 0))]},
        {"name": "gem", "verts": [(-1, -1, 0), (0, 1, 0), (1, -1, 0)],
         "faces": [(0, 1, 2, 0x00FF00, None)]},
    ])
    q = Qemu(b("kernel.img"))
    try:
        q.boot()
        assert _upload(q, mkbm.pack(MODEL_CART.encode(), title="models test", mesh=mesh))
        out = q.expect("drawn gem\t", timeout=15).decode(errors="replace")
        out += q.expect("\n").decode(errors="replace")
        assert "models\t2\ttile\tgem\tnil\tnil" in out, out
        assert "drawn tile\tff0000 0000ff\t2" in out, out
        assert "bounds\t-1.0\t-1.0\t0.0\t1.0\t1.0\t0.0" in out, out
        assert "drawn gem\t00ff00 000000\t1" in out, out
        q.expect("> ", timeout=10)
    finally:
        q.close()


KEEP_CART = r"""
function _init()
  log("before", #models())
  local p = assert(cart_load("/carts/MODELS.BM"))
  log("loaded", #models(), models()[1], p.title)
  p.title = "copy"
  log("saved", tostring(cart_save("/carts/COPY.BM", p)))
  quit()
end
"""


ANIM_CART = r"""
local m
local function hex(x, y) return string.format("%06x", pget(x, y) or 0) end
function _init()
  m = model("figure")
  local c = clips(m)
  log("clips", #c, c[1].name, c[1].length, tostring(c[1].loop), c[2].name, tostring(c[2].loop))
  log("rest", bone3d(m, "arm.R"))
  log("len", animate(m, "wave", 0.5))
  log("up", bone3d(m, 2))
  log("bad", select(2, pcall(animate, m, "dance", 0)), tostring(bone3d(m, "tail")))
end
local n = 0
function _update() n = n + 1 end
function _draw()
  cls(0)
  zclear()
  camera3d(0.5, 1, -6, 0, 0, 60)
  light3d(0, 0, -1, 1)
  if n == 2 then animate(m) end                       -- rest
  if n == 4 then animate(m, "wave", 0.5) end          -- the arm up
  if n == 6 then animate(m, "wave", 0.25, "still", 0, 1) end   -- all of "still": rest
  draw3d(m, 0, 0, 0)
  if n == 3 then log("drawn rest", hex(435, 203), hex(366, 88)) end
  if n == 5 then log("drawn up", hex(435, 203), hex(366, 88)) end
  if n == 7 then log("drawn mix", hex(435, 203), hex(366, 88)) quit() end
end
"""


def test_animation(b, opts):
    """bm Animator: a model with a skeleton (ANIM, written by the Studio's
    core in make test-studio) moves on the console: animate() poses it
    (an arm turns up around its shoulder), clips() lists the animations,
    bone3d() follows a bone, two clips mix."""
    path = b("studio-test-anim.bm")
    if not os.path.exists(path):
        print("     skipped: no build/studio-test-anim.bm (make test-studio needs Node)")
        return
    secs = dict(bmmesh.cart_sections(open(path, "rb").read()))
    cart = mkbm.pack(ANIM_CART.encode(), title="anim test", mesh=secs[bmmesh.SEC_MESH], extra=[(bmmesh.SEC_ANIM, secs[bmmesh.SEC_ANIM])])
    q = Qemu(b("kernel.img"))
    try:
        q.boot()
        assert _upload(q, cart)
        out = q.expect("drawn mix\t", timeout=15).decode(errors="replace")
        out += q.expect("\n").decode(errors="replace")
        lines = {l.split("\t")[0]: l.split("\t")[1:] for l in out.splitlines() if "\t" in l}
        assert lines["clips"] == ["2", "wave", "1.0", "true", "still", "false"], out
        assert [float(v) for v in lines["rest"]] == [1, 1, 0.5, 2, 1, 0.5], out     # head, then tail
        assert lines["len"] == ["1.0"], out
        up = [float(v) for v in lines["up"]]                # the arm turned up: the tail above the head
        assert all(abs(a - b) < 1e-4 for a, b in zip(up, [1, 1.25, 0.5, 1, 2.25, 0.5])) and len(up) == 6, out
        assert "no animation \"dance\"" in lines["bad"][0] and lines["bad"][1] == "nil", out

        def orange(h):
            c = int(h, 16)
            return (c >> 16) > 150 and (c & 255) < 100
        rest, upp, mix = lines["drawn rest"], lines["drawn up"], lines["drawn mix"]
        assert orange(rest[0]) and rest[1] == "000000", out      # the arm out to the side
        assert upp[0] == "000000" and orange(upp[1]), out        # the arm up
        assert mix == rest, out
        q.expect("> ", timeout=10)
    finally:
        q.close()


def test_sdk_keeps_models(b, opts):
    """The SDK on the console (cart_load / cart_save) writes back the
    sections it does not edit: the 3D models made with bm Studio stay."""
    T = bmmesh.TEXTURED
    mesh = bmmesh.encode([{"name": "crate", "verts": [(0, 0, 0), (0, 1, 0), (1, 1, 0), (1, 0, 0)],
                           "faces": [(0, 1, 2, T, (0, 16, 0, 0, 16, 0)), (0, 2, 3, 0x123456, None)]}])
    tmp = tempfile.mkdtemp(prefix="bm-keep-")
    img = os.path.join(tmp, "sd.img")
    src = os.path.join(tmp, "models.bm")
    with open(src, "wb") as f:
        f.write(mkbm.pack(b"function _draw() cls(0) end", title="with models", mesh=mesh))
    mksd.build(img, [(src, "carts/models.bm")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    try:
        q.boot()
        assert _upload(q, mkbm.pack(KEEP_CART.encode(), title="keep test"))
        out = q.expect("saved\t", timeout=20).decode(errors="replace")
        out += q.expect("\n").decode(errors="replace")
        assert "before\t0" in out and "loaded\t1\tcrate\twith models" in out, out
        assert "saved\ttrue" in out, out
        q.expect("> ", timeout=10)
    finally:
        q.close()
    try:
        part = os.path.join(tmp, "part.img")
        with open(img, "rb") as f, open(part, "wb") as o:
            f.seek(2048 * 512)
            o.write(f.read())
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
        saved = subprocess.run(["mtype", "-i", part, "::/CARTS/COPY.BM"], capture_output=True, env=env).stdout
        secs = dict(bmmesh.cart_sections(saved))
        assert secs.get(bmmesh.SEC_MESH) == mesh, sorted(secs)
        assert saved[24:28] == b"copy"
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def _assist_checksum(question):
    """CRC-32 of the assistant's network outputs, as scripts/assistlib.py
    computes them (the ARM must give the same: ai.checksum)"""
    import struct
    import zlib
    sys.path.insert(0, os.path.join(HERE, "..", "scripts"))
    import assistlib as al
    entries = al.parse_kb(al.kb_paths(os.path.join(HERE, "..", "src", "ai", "kb")))
    classes, net = al.load_weights(os.path.join(HERE, "..", "src", "ai", "assist.weights"))
    out = al.logits(al.entry_net(entries, classes, net), al.features(question))
    return "%08x" % (zlib.crc32(struct.pack("<%di" % len(out), *out)) & 0xFFFFFFFF)


def test_assistant(b, opts):
    """M30: the development assistant (monitor A, the Dev tab's Assistant):
    a question typed on the serial line, answered while typing, Enter
    inserts the code; F7 and a request draw a sprite into the sheet; F8
    times the network on this CPU; Esc leaves. The tools open the same
    panel (require "assist") with F6."""
    q = Qemu(b("kernel.img"))

    def k(s, gap=0.05):
        for c in s:
            q.send(c)
            time.sleep(gap)

    def see(words, tries=40):
        for _ in range(tries):
            _, text = settled_screen(q, lambda i, t: all(any(w in l for l in t) for w in words), tries=2)
            if all(any(w in l for l in text) for w in words):
                return text
            time.sleep(0.25)
        raise AssertionError(f"not on screen: {words}\n" + "\n".join(text))

    def shot(name):
        if opts.shots:
            _save_png(q.screendump(), os.path.join(opts.shots, f"assistant-{name}.png"))
    try:
        q.boot()
        k("I")
        out = q.expect("sprite recipes", timeout=20).decode(errors="replace")
        assert re.search(r"assistant: ready, \d+ entries, \d+ sprite recipes", out), out
        # the network on the ARM (SIMD) gives the integers of the Python reference
        out = q.expect("assistant: checksum ", timeout=10).decode(errors="replace")
        got = q.expect("\n", timeout=5).decode().strip()
        assert got == _assist_checksum("come muovo il personaggio con le frecce"), \
            f"ARM checksum {got}, Python {_assist_checksum('come muovo il personaggio con le frecce')}"
        see(["Assistant", "type a question"])
        k("come muovo il personaggio con le frecce")
        see(["Muovere un personaggio con le frecce"])
        shot("question")
        k("\r")
        q.expect("assistant: inserted", timeout=10)
        see(["btn(0)", "code inserted"])
        k("\x1b[18~")                                       # F7: a sprite
        see(["Assistant", "sprite"])
        k("slime rosso")
        see(["Slime"])
        shot("sprite")
        k("\r")
        out = q.expect("x16", timeout=10).decode(errors="replace")
        assert "assistant: sprite slime 16x16" in out, out
        see(["in the sheet at 0,0"])
        k("\x1b[19~")                                       # F8: the speed test
        out = q.expect("ms each", timeout=60).decode(errors="replace")
        assert "assistant: speed 100 questions" in out, out
        # the answers of the ARM code (SIMD) are the right ones
        see(["Collisione tra due rettangoli", "Saltare con la gravit", "Muovere un personaggio"])
        shot("tool")
        k("\x1b[20~")                                       # F9: an error explained
        see(["line 12: did you mean spr?", "attempt to call a nil value"])
        shot("error")
        k("\x1b", 0.5)                                     # Esc closes the panel
        see(["bm assistant", "sprite", "speed"])          # its bar: the keys as chips
        k("\x1b", 0.5)                                     # Esc: back to the monitor
        out = q.expect("> ", timeout=10).decode(errors="replace")
        assert "error" not in out, out
    finally:
        q.close()


def test_code_editor(b, opts):
    """bm Code (Dev tab, monitor C): opens a cartridge with a long name and
    sprites from the SD and a second one in another tab, two pages side by
    side, edits and saves only the code (sheet and map stay as they were,
    the long name too), runs a game that stops with an error and comes back
    on the line, F9 explains it, an "#entry:" line done by the assistant
    and undone; on leaving, the unsaved tab is kept for later. The card is
    still a clean FAT32 volume."""
    tmp = tempfile.mkdtemp(prefix="bm-code-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [(b("demo.bm"), "carts/Il mio demo.bm"), (b("carts/pong.bm"), "carts/pong.bm")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    log = []

    def k(s, gap=0.04):
        # an escape sequence goes in one piece: a lone Esc is one after 6 frames
        for c in re.findall(r"\x1b\[[0-9]*[~A-Z]|\x1bO[A-Z]|.", s, re.S):
            q.send(c)
            time.sleep(gap)

    def expect(needle, timeout=15):
        out = q.expect(needle, timeout=timeout).decode(errors="replace")
        log.append(out)
        return out

    def see(words, tries=40):
        text = []
        for _ in range(tries):
            img_ = q.screendump()
            text = screen_text(img_, 6, 12)
            if all(any(w in l for l in text) for w in words):
                return text
            time.sleep(0.25)
        raise AssertionError(f"not on screen: {words}\n" + "\n".join(text))

    def shot(name):
        if opts.shots:
            _save_png(q.screendump(), os.path.join(opts.shots, f"code-{name}.png"))
    try:
        q.boot()
        k("C")
        expect("code: ready")
        see(["keys"])                                       # the F1 chip, then "keys"
        k("\x0f", 0.5)                                      # Ctrl+O: the files
        see(["Open a cartridge", "/carts/Il mio demo.bm", "/carts/pong.bm"])
        shot("open")
        k("\r")
        expect("code: opened /carts/Il mio demo.bm")
        k("\x0f", 0.5)
        k("\x1b[B", 0.3)
        k("\r")
        expect("code: opened /carts/pong.bm")
        k("\x1bOS", 0.5)                                    # F4: two pages
        text = see(["Il mio demo.bm", "pong.bm 1", "two pages"])
        assert "untitled" not in text[0], "the untouched first tab gave its place: " + text[0]
        shot("split")
        k("\x1bOS", 0.5)                                    # one page again

        # pong: a line on top, saved (only the code changes)
        k("\x0c")                                           # Ctrl+L: go to line
        k("1\r", 0.1)
        k("-- edited by bm Code\r")
        k("\x13")                                           # Ctrl+S
        expect("code: saved /carts/pong.bm")

        # the demo (long name, sprites): an error on line 1, then F5
        k("\x1bOQ", 0.3)                                    # F2: the other tab
        k("\x0c")
        k("1\r", 0.1)
        k('error("boom")\r')
        k("\x1b[15~")                                       # F5: save and run
        expect("code: saved /carts/Il mio demo.bm")
        expect("main.lua:1: boom", timeout=30)
        expect("code: ready", timeout=30)
        see(["the game stopped"])
        shot("error")
        k("\x1b[20~", 0.5)                                  # F9: explained
        see(["Assistant", "error"])
        k("\x1b", 0.6)                                     # the panel closes

        # a new tab, a function, an "#entry:" line above its if
        k("\x14", 0.3)                                      # Ctrl+T
        k("function f(v)\rif v > 0 then\rr = 1\relse\rr = 2\rend\rend")
        k("\x0c")
        k("1\r", 0.1)
        k("\x1b[F", 0.1)                                    # End
        k("\r#entry: usa il ternario #\r")
        expect("code: #entry usa il ternario")
        out = expect("\n")
        assert "if/else" in out and "not done" not in out, out
        see(["r = v > 0 and 1 or 2"])
        shot("entry")
        k("\x1a", 0.3)                                      # Ctrl+Z
        see(["if v > 0 then"])

        # a new cartridge: Ctrl+N, the name offered, Ctrl+S writes it
        k("\x0e", 0.5)                                      # Ctrl+N
        see(["New cartridge, file name", "GAME1.BM"])
        k("\r", 0.5)
        k("\x13")
        expect("code: saved /carts/GAME1.BM")

        # leave: the untitled tab has changes; kept for later
        k("\x1b", 0.6)                                     # Esc: the menu
        see(["New cartridge", "Exit"])
        k("\x1b[A", 0.3)                                   # up from the first: Exit
        k("\r", 0.5)
        see(["not saved"])
        k("k", 0.3)                                        # Keep for later
        expect("> ", timeout=10)
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
        out = os.path.join(tmp, "demo.bm")
        subprocess.run(["mcopy", "-i", part, "::/CARTS/Il mio demo.bm", out], check=True, env=env)
        sys.path.insert(0, os.path.join(HERE, "..", "scripts"))
        new = open(out, "rb").read()
        old = open(b("demo.bm"), "rb").read()
        def sections(d):
            import struct
            n = d[17]
            res = {}
            for i in range(n):
                t, off, size, _ = struct.unpack_from("<IIII", d, 128 + 16 * i)
                res[t] = d[off:off + size]
            return res
        ns, os_ = sections(new), sections(old)
        assert ns[1].startswith(b'error("boom")'), ns[1][:40]
        assert ns[2] == os_[2] and ns[3] == os_[3], "the sheet and the map changed"
        subprocess.run(["mcopy", "-i", part, "-o", "::/CARTS/PONG.BM", os.path.join(tmp, "pong.bm")],
                       check=True, env=env)
        assert open(os.path.join(tmp, "pong.bm"), "rb").read().find(b"-- edited by bm Code") > 0
        subprocess.run(["mcopy", "-i", part, "-o", "::/CARTS/GAME1.BM", os.path.join(tmp, "game1.bm")],
                       check=True, env=env)
        g = sections(open(os.path.join(tmp, "game1.bm"), "rb").read())
        assert list(g) == [1] and g[1].startswith(b"-- my game"), g.keys()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def test_editor(b, opts):
    """M15: the editor makes a new game, saves it on the SD card, tries it,
    comes back; a game that stops with an error brings the editor to the
    line; the card is still a clean FAT32 volume."""
    tmp = tempfile.mkdtemp(prefix="bm-ed-")
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
        see("bm editor")
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
        out = q.expect('bm: "New game"', timeout=20).decode(errors="replace")
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
        assert saved[:8] == b"BMCART\x00\x00" and b"error('boom')" in saved, saved[:200]
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def test_bm_upload_errors(b, opts):
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
            assert _upload(q, _bm(src)), src
            out = q.expect("> ", timeout=30).decode(errors="replace")
            assert "stopped with an error" in out and needle in out, out
            img = q.screendump()
            assert img[:2] == (640, 360), "console restored after the error"
        # sandbox: no file loading
        assert _upload(q, _bm("function _init() assert(dofile == nil and load == nil and io == nil and os == nil) error('sandbox ok') end"))
        assert b"sandbox ok" in q.expect("> ", timeout=20)
        # a working cart that exits by itself is not needed: 'q' stops it
        assert _upload(q, _bm("function _draw() cls(0x102030) print('hello', 8, 16) end"))
        time.sleep(1.0)
        img, _ = settled_screen(q, lambda i, t: "hello" in t[1])
        assert pixel(img, 300, 300) == (16, 32, 48), pixel(img, 300, 300)
        assert "hello" in screen_text(img)[1], screen_text(img)[:3]
        q.send("q")
        q.expect("update+draw", timeout=10)
        q.expect("> ")
    finally:
        q.close()


def test_upload_refused_and_corrupt(b, opts):
    q = Qemu(b("kernel.img"))
    try:
        q.boot()
        # the old .cart format is no longer played (its interpreter is gone)
        assert _upload(q, b"S32CART1" + bytes(200))
        q.expect("unknown cartridge format", timeout=10)
        q.expect("> ")
        # CRC failure is reported, the monitor carries on
        q.send("U")
        q.expect("15 s timeout\r\n")
        import struct, zlib
        data = b"BMCART\x00\x00" + b"x" * 100
        q.send(b"BMLD" + struct.pack("<II", len(data), zlib.crc32(data) ^ 1))
        loader = bm_load.Loader(q.port, echo=q)
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


def test_nano8(b, opts):
    """M23: nano8, from the Games tab, lists the .p8 / .p8.png carts on the
    SD card, plays the API test cart (all its checks pass on the console's
    kernel) and shows its screen, pauses with Start, goes back to the list,
    plays a cart whose _init is longer than a frame allows (time slices,
    no timeout) and a .p8.png cart; Esc (q) leaves nano8 suspended."""
    tmp = tempfile.mkdtemp(prefix="bm-n8-")
    img = os.path.join(tmp, "sd.img")
    api = os.path.join(HERE, "nano8", "carts", "api.p8")
    heavy = os.path.join(HERE, "nano8", "carts", "heavy.p8")
    png = os.path.join(HERE, "..", "carts", "nano8", "roms", "sixlets2.p8.png")
    mksd.build(img, [(b("carts/nano8.bm"), "carts/nano8.bm"), (api, "carts/nano8/api.p8"),
                     (heavy, "carts/nano8/heavy.p8"), (png, "carts/nano8/sixlets2.p8.png")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    try:
        q.expect(MENU, timeout=30)
        time.sleep(0.5)
        q.send("\r")                           # the only game: nano8
        q.expect("playing nano8.bm", timeout=10)
        time.sleep(2.0)
        q.send(" ")                            # A: the first cart, api.p8
        q.expect("nano8: playing /carts/nano8/api.p8", timeout=20)
        out = q.expect("checks, ", timeout=60) + q.expect("\n", timeout=5)
        out += q.expect("api: all tests passed", timeout=5)
        assert b"fail " not in out, out
        time.sleep(1.0)
        # its screen: colour 3 (dark green) in the 2x area at (192, 52)
        w, h, px = q.screendump()
        i = ((52 + 200) * w + 192 + 200) * 3
        r, g, bl = px[i], px[i + 1], px[i + 2]
        assert g > 100 and r < 40 and bl < 120, (r, g, bl)
        q.send("\r")                           # Start: the pause menu
        q.expect("nano8: paused", timeout=10)
        time.sleep(0.5)
        for _ in range(5):                     # down to "Back to the list"
            q.send("s")
            time.sleep(0.4)
        q.send(" ")
        q.expect("nano8: back to the list", timeout=10)
        time.sleep(1.0)
        q.send("d")
        time.sleep(0.4)
        q.send(" ")                            # an _init longer than a frame allows
        q.expect("nano8: playing /carts/nano8/heavy.p8", timeout=20)
        out = q.expect("heavy: done 9000", timeout=180)
        assert b"timeout" not in out, out
        q.send("\r")
        q.expect("nano8: paused", timeout=10)
        time.sleep(0.5)
        for _ in range(5):
            q.send("s")
            time.sleep(0.4)
        q.send(" ")
        q.expect("nano8: back to the list", timeout=10)
        time.sleep(1.0)
        q.send("d")
        time.sleep(0.4)
        q.send(" ")                            # the next cart: a .p8.png
        q.expect("nano8: playing /carts/nano8/sixlets2.p8.png", timeout=20)
        time.sleep(4.0)
        assert b"nano8: Runtime error" not in q.buf, q.buf
        q.send("q")
        q.expect('"nano8" suspended', timeout=10)
    finally:
        q.close()
        shutil.rmtree(tmp, ignore_errors=True)


def test_chainloader(b, opts):
    q = Qemu(b("chainloader.img"))
    try:
        loader = bm_load.Loader(q.port, echo=q)
        assert loader.wait_ready(timeout=10), "chainloader did not announce itself"
        assert b"bm chainloader" in q.buf
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
        loader = bm_load.Loader(q.port, echo=q)
        assert loader.wait_ready(timeout=10)
        import struct
        q.send(b"BMLD" + struct.pack("<II", 4, 0xDEADBEEF))
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
