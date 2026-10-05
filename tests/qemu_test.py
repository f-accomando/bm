#!/usr/bin/env python3
"""
End-to-end tests in QEMU (-M raspi0). Images are loaded at 0x8000 through
-bios, exactly as the Pi firmware does. The serial port is a TCP socket.
--kernel7: the same tests with kernel7.img (the Pi Zero 2 W's build) in
raspi2b (a Pi 2 B: the BCM2710's peripherals, a Cortex-A7, no radio).

  tests/qemu_test.py [--build build] [--update-ref] [-k name] [--kernel7]
"""
import argparse
import base64
import hashlib
import re
import os
import shutil
import socket
import struct
import zlib
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


# --kernel7: kernel7.img in raspi2b (main)
KERNEL7 = False
# kernel7.img is built (make ZERO2=1, or --kernel7): in the SD image too
ZERO2 = os.environ.get("ZERO2", "0") == "1"
# tests that need a BCM2835 board, skipped with --kernel7
BCM2835_ONLY = {
    "test_pi1_board": "a Pi 1 (raspi1ap)",
    "test_wifi_probe": "the radio chip (raspi2b is a Pi 2 B: none)",
    "test_bt_": "the radio chip (raspi2b is a Pi 2 B: none)",
    "test_menu_tabs": "a Bluetooth pad (the radio chip)",
    "test_stick_pointer": "a Bluetooth pad (the radio chip)",
    "test_chainloader": "the serial chainloader (ARMv6)",
}

# --shard K/N: the tests split into N groups of about the same time (the CI
# runs them on N machines at once). Seconds on the CI (2026-10-05) of the
# tests over 20 s; the others count 5. A test missing here only makes the
# groups less even.
SLOW = {
    "test_overbit": 79, "test_yharnam": 76, "test_chainloader": 70, "test_studio_animator": 61,
    "test_home_ui": 56, "test_stress_monitor": 48, "test_pad_typing": 48, "test_lib_tab": 46,
    "test_kitchen": 45, "test_studio_assistant": 44, "test_editor": 39, "test_code_completion": 38,
    "test_titan": 36, "test_games": 34, "test_picture_model": 32, "test_menu_tabs": 31,
    "test_sdk_suite": 29, "test_pixel_big": 26, "test_mouse_cart": 23, "test_market": 23,
    "test_code_editor": 23, "test_update": 22, "test_room_bench": 22, "test_bm_boot_demo": 22,
    "test_nano8": 20, "test_meshy2mesh": 20,
}


def shard(tests, k, n):
    """Group k (1..n) of n: the longest tests first, each to the group with
    the least time so far; inside a group the order of the file."""
    load, mine = [0] * n, set()
    for i, (name, _) in sorted(enumerate(tests), key=lambda t: (-SLOW.get(t[1][0], 5), t[0])):
        g = load.index(min(load))
        load[g] += SLOW.get(name, 5)
        if g == k - 1:
            mine.add(i)
    return [t for i, t in enumerate(tests) if i in mine]


class Qemu:
    def __init__(self, image, extra=(), mini_uart=False, machine=None):
        """mini_uart: the second serial port (the mini UART, where the
        console goes when the PL011 is given to Bluetooth) on a socket too,
        as self.mini. machine: raspi1ap is a Pi 1 A+ (same SoC); by
        default raspi0, raspi2b with --kernel7."""
        machine = machine or ("raspi2b" if KERNEL7 else "raspi0")
        self.tmp = tempfile.mkdtemp(prefix="bm-")
        self.mon_path = os.path.join(self.tmp, "mon.sock")
        self.qmp_path = os.path.join(self.tmp, "qmp.sock")
        tcp, tcp2 = free_port(), free_port()
        self.proc = subprocess.Popen(
            [QEMU, "-M", machine, "-bios", image, "-display", "none",
             "-serial", f"tcp:127.0.0.1:{tcp},server=on,wait=on",
             "-serial", f"tcp:127.0.0.1:{tcp2},server=on,wait=on" if mini_uart else "null",
             "-monitor", f"unix:{self.mon_path},server=on,wait=off",
             "-qmp", f"unix:{self.qmp_path},server=on,wait=off", *extra],
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

    def input_events(self, events):
        """QMP input-send-event: the monitor's mouse_move is relative only,
        an absolute position (usb-tablet) needs this."""
        import json
        with socket.socket(socket.AF_UNIX) as s:
            s.connect(self.qmp_path)
            f = s.makefile("rw")
            f.readline()
            for cmd in ({"execute": "qmp_capabilities"},
                        {"execute": "input-send-event", "arguments": {"events": events}}):
                f.write(json.dumps(cmd) + "\n")
                f.flush()
                reply = f.readline()
                assert '"return"' in reply, reply
        time.sleep(0.1)

    def pointer(self, x, y, w=640, h=360):
        """The tablet to pixel (x, y) of a w x h screen (the pointer's
        position is a fraction of the screen)."""
        self.input_events([{"type": "abs", "data": {"axis": "x", "value": int((x + 0.5) * 32767 / w)}},
                           {"type": "abs", "data": {"axis": "y", "value": int((y + 0.5) * 32767 / h)}}])

    def key(self, name, down):
        """A key of the USB keyboard pressed or let go (QMP, a qcode such as
        "f12"): the monitor's sendkey queues the next key after the release,
        this one holds a key while others are typed."""
        self.input_events([{"type": "key", "data": {"down": down, "key": {"type": "qcode", "data": name}}}])

    def click(self, button="left"):
        """A press and a release of a mouse button (left, right, middle,
        wheel-up, wheel-down)."""
        for down in (True, False):
            self.input_events([{"type": "btn", "data": {"down": down, "button": button}}])
            time.sleep(0.1)

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
        board = b"board a21041" if KERNEL7 else b"board 920092"   # raspi2b: a Pi 2 B
        for s in (b"bm\x1b[0m kernel", board, b"screen 640x360",
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
        # nothing pressed yet: the keyboard's keys (on the Pi keyboard and
        # mouse come first), Enter to play, Ctrl+Shift+Esc for the monitor
        _, text = settled_screen(q, lambda i, t: "Monitor" in t[21])
        img = q.screendump()
        text = screen_text(img)
        assert "Games" in text[1] and "Dev" in text[1], text[1]
        col = text[21].index("Monitor")
        colours = {pixel(img, x, 21 * 16 + y) for x in range(col * 8, col * 8 + 56) for y in range(16)}
        assert any(r > 230 and g > 230 and b > 230 for r, g, b in colours), \
            f"hint text not rendered {colours}"
        widths = [x1 - x0 for x0, x1 in prompt_spans(img, 21)]
        assert len(widths) == 4 and widths[0] >= 34 and widths[1] >= 28 and widths[2] >= 28 and \
            20 <= widths[3] <= 28, widths                                       # Enter, Ctrl, Shift, Esc
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
    spans [x0, x1) with light pixels in rows 8-39, right of the tabs (Settings,
    the last, ends at x 352 since the Lib tab)."""
    runs, start = [], None
    for x in range(362, 640):
        lit = any(sum(pixel(img, x, y)) > 450 for y in range(8, 40))
        if lit and start is None:
            start = x
        elif not lit and start is not None:
            runs.append((start, x))
            start = None
    if start is not None:
        runs.append((start, 640))
    return runs


def wait_screen(q, cond, timeout=6.0):
    """A screendump once cond(shot) holds (or the last one at the timeout):
    after a new selection the menu blurs a cover for its background, slow
    in QEMU on a busy PC, so a fixed sleep is not enough."""
    deadline = time.time() + timeout
    while True:
        shot_ = q.screendump()
        if cond(shot_):
            return shot_
        if time.time() > deadline:
            print("    wait_screen: still not there after %.0f s" % timeout)
            return shot_
        time.sleep(0.2)


def title_is(t):
    """cond for wait_screen: the menu's title pill shows t."""
    return lambda shot_: t in screen_text(shot_)[4]


def wait_icons(q, n, timeout=6.0):
    """A screendump of the menu once it is drawn (the tabs) and its bar has
    n status icons: the first frame of the menu comes 0.5-1 s after
    "cartridge menu", later on a busy PC."""
    deadline = time.time() + timeout
    while True:
        shot_ = q.screendump()
        if ("Games" in screen_text(shot_)[1] and len(bar_icons(shot_)) == n) or time.time() > deadline:
            return shot_
        time.sleep(0.2)


def wait_bar_icons(q, ok, tries=16):
    """The menu bar once its icons are what ok(runs, img) wants: QEMU shows
    page 0 even while it is drawn, and a USB device can be announced a
    moment after the menu, so one screendump can miss them. Returns
    (img, runs), the last ones if they never get there."""
    for _ in range(tries):
        img = q.screendump()
        runs = bar_icons(img)
        if ok(runs, img):
            break
        time.sleep(0.5)
    return img, runs


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
    (top, bottom) of its light pixels at x 627, y 96-292, or None."""
    ys = [y for y in range(96, 292) if sum(pixel(img, 627, y)) > 600]
    return (ys[0], ys[-1] + 1) if ys else None


def tabs_lit(img):
    """Which tabs of the menu bar are on their light pill (M27, Market since
    M25, Lib after Dev): Market, Games, Dev, Lib, Settings, from a pixel of
    the pill left of each name. The Market waits off the screen at the left
    (2026-10-04): on it the tabs are where they were, elsewhere 7 columns
    to the left."""
    if sum(pixel(img, 20, 24)) > 600:
        return ["Market"]
    return [name for name, x in (("Games", 44), ("Dev", 116), ("Lib", 172), ("Settings", 228))
            if sum(pixel(img, x, 24)) > 600]


def img_tabs(q, want=None):
    """the tabs lit on the screen (a few tries for `want`: a frame may be half drawn)"""
    for _ in range(8):
        lit = tabs_lit(q.screendump())
        if want is None or lit == want:
            break
        time.sleep(0.2)
    return lit


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
        sendkeys(q, "ctrl-shift-esc")          # Ctrl+Shift+Esc: from the menu to the monitor
        q.expect("back to the monitor", timeout=10)
        q.expect(PROMPT, timeout=10)
        q.expect("> ")

        # a game (the system's keys): Esc is its menu (Start), Ctrl+Esc leaves it (PS)
        sendkeys(q, "n")
        time.sleep(1.5)
        q.buf = b""
        sendkeys(q, "esc")
        time.sleep(1.0)
        q.buf += q.port.read(0.3)
        assert b"update+draw" not in q.buf, "Esc left the game"
        sendkeys(q, "ctrl-esc")
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


def holdkey(q, key, ms):
    """A key held on the emulated USB keyboard for ms milliseconds."""
    with socket.socket(socket.AF_UNIX) as s:
        s.connect(q.mon_path)
        s.sendall(f"sendkey {key} {ms}\n".encode())
        time.sleep(0.05)


def test_keys_help(b, opts):
    """The system's keys (src/kernel/syskeys.c, 2026-10-04): F12 held over
    the menu shows them with the keys' pictures, then the menu's own; let
    go, the menu again. In an app (bm Studio) the same, then the app's
    (keyhelp()); Ctrl+Esc leaves it."""
    q = Qemu(b("kernel.img"), USB_KBD)
    try:
        q.expect(MENU, timeout=30)
        time.sleep(1.0)
        holdkey(q, "f12", 2500)
        img, rows = settled_screen(q, lambda i, t: any("(F12 held)" in l for l in t) and
                                   any("read the SD card again" in l for l in t), tries=40)
        text = "\n".join(rows)
        for want in ("Keys", "(F12 held)", "hold: the keys", "back to bm's menu (PS)", "the monitor", "save as",
                     "performance overlay", "Menu", "options of the game", "read the SD card again"):
            assert want in text, f"{want!r} not in the keys:\n{text}"
        if opts.shots:
            _save_png(img, os.path.join(opts.shots, "keys-help-menu.png"))
        time.sleep(2.0)                         # let go: the menu
        _, text = settled_screen(q, lambda i, t: not any("(F12 held)" in l for l in t), tries=40)
        assert not any("(F12 held)" in l for l in text) and any("Settings" in l for l in text), "\n".join(text)
        # in an app: the system's keys, then the app's (keyhelp()): bm Studio
        sendkeys(q, "ctrl-shift-esc")
        q.expect(PROMPT, timeout=10)
        q.expect("> ")
        q.send("3")
        _, text = settled_screen(q, lambda i, t: any("bm Studio" in l for l in t), tries=60)

        def help_shows(*words, tries=40):
            img_, t_ = settled_screen(q, lambda i, t: all(any(w in l for l in t) for w in words), tries=tries)
            joined = "\n".join(t_)
            for w in words:
                assert w in joined, f"{w!r} not in bm Studio's keys:\n{joined}"
            return img_
        q.key("f12", True)
        img = help_shows("(F12 held)", "back to bm's menu (PS)", "bm Studio", "the model before / after",
                         "models", "assistant")
        if opts.shots:
            _save_png(img, os.path.join(opts.shots, "keys-help-studio.png"))
        sendkeys(q, "f1")                       # F1 with F12 held: the build page's keys
        help_shows("1/", "a level up / down")
        sendkeys(q, "down")                     # the next page of them
        img = help_shows("2/", "the faces from behind")
        if opts.shots:
            _save_png(img, os.path.join(opts.shots, "keys-help-studio-2.png"))
        q.key("f12", False)
        time.sleep(1.0)
        sendkeys(q, "ctrl-esc")                 # Ctrl+Esc: back where it came from
        q.expect("> ", timeout=10)
    finally:
        q.close()


def test_usb_hub(b, opts):
    """Devices behind a hub (the Pi 1 B's USB ports are all behind its
    LAN951x): each port reset and enumerated; the keyboard types, the
    tablet is the mouse (M32). QEMU's hub is full speed, so no split
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
        sendkeys(q, "ctrl-shift-esc")          # Ctrl+Shift+Esc: from the menu to the monitor
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
    # only the tablet behind the hub: the mouse, no keyboard or gamepad
    q = Qemu(b("kernel.img"), ["-device", "usb-hub,port=1", "-device", "usb-tablet,port=1.3"])
    try:
        out = q.expect(MENU, timeout=40)
        assert b"usb: tablet (mouse) 0627:0001 'QEMU USB Tablet' (hub port 3), wheel" in out, out
        assert b"usb: no keyboard or gamepad" not in out, out
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
        q.expect("bm: loaded", timeout=10)    # the system's splash, then the game
        time.sleep(1.0)
        q.send("q")
        out = q.expect("update+draw", timeout=15).decode(errors="replace")
        assert '"bm native demo"' in out, out
        q.send("d\r")                         # next: /carts/demo2.bm
        _, text = settled_screen(q, lambda i, t: any("Close bm native demo?" in l for l in t))
        assert any("Close bm native demo?" in l for l in text), "\n".join(text)
        q.send("\r")                          # the demo was suspended: close it
        q.expect("playing demo2.bm", timeout=10)
        q.expect("bm: loaded", timeout=10)
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


ONLINE_CART = r"""
local n = 0
function _init() online(true, "You are the host: the match ends for all.") log("online start") end
function _update()
  n = n + 1
  local _, asking = online()
  if n % 60 == 0 then log("frame " .. n .. (asking and " asking" or "")) end
  if btnp(5) then log("game saw B") end
end
function _leave() log("online leave " .. n) end
function _draw() cls(0x203040) print("frame " .. n, 8, 8, 0xFFFFFF) end
"""


def test_online_leave(b, opts):
    """PS in a game played online (online(true), 2026-10-04): not suspended
    but "Leave the match?" over the game, which goes on; B stays (the game
    never sees it), PS again leaves through _leave() and the game ends."""
    tmp = tempfile.mkdtemp(prefix="bm-online-")
    img = os.path.join(tmp, "sd.img")
    cart = os.path.join(tmp, "online.bm")
    with open(cart, "wb") as f:
        f.write(mkbm.pack(ONLINE_CART.encode(), title="AAA online"))
    mksd.build(img, [(cart, "carts/online.bm")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    try:
        q.expect(MENU, timeout=30)
        time.sleep(0.5)
        q.send("\r")
        q.expect("online start", timeout=10)
        q.expect("frame 60", timeout=10)
        q.send("\x1c")                          # Ctrl+\: PS
        q.expect("leave the online game?", timeout=10)
        _, text = settled_screen(q, lambda i, t: any("Leave the match?" in l for l in t)
                                 and any("disconnect from the server" in l for l in t))
        assert any("Leave the match?" in l for l in text), "\n".join(text)
        assert any("You will leave the game and" in l for l in text), "\n".join(text)
        assert any("the match ends for all" in l for l in text), "\n".join(text)
        assert any("Leave" in l and "Stay" in l for l in text), "\n".join(text)
        out = q.expect(" asking", timeout=10).decode(errors="replace")   # the game goes on
        q.send("x")                             # B: stays
        out = q.expect("stays in the online game", timeout=10).decode(errors="replace")
        time.sleep(1.5)
        _, text = settled_screen(q, lambda i, t: not any("Leave the match?" in l for l in t))
        assert not any("Leave the match?" in l for l in text), "\n".join(text)
        q.send("\x1c")
        q.expect("leave the online game?", timeout=10)
        time.sleep(0.3)
        q.send("\x1c")                          # PS again: yes
        out = q.expect("left the online game", timeout=10).decode(errors="replace")
        assert "online leave" in out and "game saw B" not in out, out
        out = q.expect('"AAA online" ', timeout=10) + q.expect("frames", timeout=10)
        out = out.decode(errors="replace")      # closed, with its numbers: not suspended
        assert "suspended" not in out, out
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
    and their submenus; the tools of the Dev tab (3) on the text console and
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
        # (with the tools, bm Mesh, bm Pixel and the info rows are below: the panel scrolls)
        screen(["AAA saver", "Resume", "Close the game", "Open in the SDK", "Open in bm Code",
                "Open in the Sound editor", "Open in bm Studio"])
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
        q.send("\x1b")                          # the project page -> the SDK's menu
        screen(["Exit bm SDK"])
        q.send("\x1b[A")                        # up: Exit bm SDK (one sequence)
        time.sleep(0.3)
        keys("\r")
        screen(["Games", "AAA saver", "last: SDK on saver.bm"])

        # the other cartridge leaves the SD card
        keys("d")
        keys("x")
        screen(["BBB delete me", "Play", "Open in bm Studio"])
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

        # settings: the same sections as the RGB30's (settings.c); the
        # keyboard layout changes and is saved; the submenus
        keys("5")
        net = "Network" if KERNEL7 else "WiFi and network"     # raspi2b: a Pi 2 B, Ethernet only
        screen(["Settings", "Controllers", net, "Screen and sound", "Updates", "Reports", "System"])
        shot("settings")
        keys("\r")                              # Controllers
        screen(["Settings > Controllers", "Player 1", "keyboard / USB", "Bluetooth keyboard",
                "Mouse"])
        keys("w")                               # up from the first: round to the last rows
        screen(["Pair a mouse", "Test the buttons", "Keyboard layout", "Button icons", "Scan the USB again",
                "Forget all controllers"])
        keys("www")                             # the keyboard layout
        _, text = settled_screen(q, lambda i, t: any("< Italian >" in l or "< US >" in l for l in t))
        before = "Italian" if any("< Italian >" in l for l in text) else "US"
        assert before == "Italian" or any("< US >" in l for l in text), "\n".join(text)
        keys("d")
        after = "US" if before == "Italian" else "Italian"
        screen([f"< {after} >", "keyboard layout: "])
        keys("\r")                              # A changes it too: back as it was
        screen([f"< {before} >"])
        keys("q")
        keys("s")
        keys("\r")
        if KERNEL7:
            screen(["Settings > Network", "Ethernet", "no cable", "port 3333"])
        else:
            screen(["Settings > WiFi and network", "Network", "none saved", "port 3333"])
            keys("w")                           # the list scrolls to its last row
            screen(["Connect to a network", "Connect at boot", "Test the connection"])
            keys("w")
            screen(["Connect at boot", "< On >"])
        keys("q")
        keys("s")
        keys("\r")                              # Screen and sound
        screen(["Settings > Screen and sound", "Game drawing (.bm)", "3D of the games", "ARM (no GPU)",
                "3D anti-aliasing", "Off",      # QEMU has no V3D; no anti-aliasing unless asked
                "3D vertices"])                 # nor the vertex shader (M36)
        keys("sssss")                           # the dev kit's overlay: simple, detailed, off again
        screen(["< Off >", "fps, ms, Lua instructions"])
        keys("\r")
        screen(["< Simple >", "performance overlay: Simple"])
        keys("\r")
        screen(["< Detailed >", "performance overlay: Detailed"])
        keys("\r")
        screen(["< Off >", "performance overlay: Off"])
        keys("s")                               # the volume: left/right, saved
        screen(["Volume", "< 10 / 10 >"])
        keys("a")
        screen(["< 9 / 10 >", "volume: 9 / 10"])
        keys("d")
        screen(["< 10 / 10 >"])
        keys("ss")
        screen(["Test pattern", "Test the sound", "HDMI sound status"])
        keys("q")
        keys("s")
        keys("\r")
        screen(["Settings > Updates", "Version", "Check for updates", "not checked"])
        keys("q")
        keys("s")
        keys("\r")
        screen(["Settings > Reports", "On the SD card", "none waiting", "Send the reports", "Report the log"])
        keys("q")
        keys("s")
        keys("\r")
        screen(["Settings > System", "Version", "Board", "SD card", "FAT32"])
        shot("system")
        keys("w")                               # the list scrolls to its last rows
        screen(["3D driver", "bm3d", "as 0.2", "Log since boot",   # QEMU: the ARM's 3D
                "Open the monitor"])
        keys("q")
        keys("s")                               # the last two of Settings (2026-10-04)
        screen(["System", "Restart"])
        keys("s")
        screen(["Restart", "Shut down"])
        keys("w")
        keys("w")
        keys("q")
        time.sleep(0.5)

        # the Dev tab: a tool on the text console, then A goes back
        keys("3")
        screen(["bm SDK", "editor (built-in)"])
        keys("d")                               # the covers' names are on pictures: the pill
        screen(["bm Sound", "sound (built-in)"])
        keys("d")
        screen(["bm Studio", "studio (built-in)"])
        keys("d")
        screen(["bm Animator", "animator (built-in)"])
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
        # a cartridge for each of the Makefile's GAMES (a new game: one more)
        with open(os.path.join(root, "Makefile")) as f:
            games = re.search(r"^GAMES\s*:=(.*)$", f.read(), re.M).group(1).split()
        assert f"FAT32, 63 MiB, label BM; {len(games)} cartridges" in out, out
        time.sleep(0.5)
        want = ("Pong", "Snake", "Star Shooter", "Chaos Kitchen", "Studio Village", "nano8")
        titles = want + ("Astro Wing", "Titan Clash", "Hunter's Night", "Overbit", "Yharnam", "Pad Typing")

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
        for title in ("bm native demo", "bm stress test", "Texture Room"):   # not games: in the kernel
            assert title not in screen and title not in seen, screen
        q.send("q")
        q.expect(PROMPT)
        q.expect("> ")
        q.send("f")
        out = q.expect('Star Shooter"\r\n').decode(errors="replace")
        assert "/carts/pong.bm" in out and "/carts/shooter.bm" in out, out
        q.close()

        # the Pi 1 image: same kernel and games, no WiFi/Bluetooth firmware
        # (and no kernel7.img, the Pi Zero 2 W's)
        for n in ("BCM43430A1.hcd", "SYN43430B0.hcd"):
            with open(os.path.join(fw, n), "wb") as f:
                f.write(b"placeholder")
        subprocess.run(["make", "-s", "-C", root, "image", "image-pi1", f"FW_DIR={fw}",
                        f"DIST={tmp}", f"BUILD={os.path.abspath(b('.'))}"],
                       check=True, stdout=subprocess.DEVNULL)
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
        def ls(name):
            return subprocess.run(["mdir", "-i", f"{os.path.join(tmp, name)}@@1M", "-b", "-/", "::"],
                                  env=env, capture_output=True, text=True).stdout
        assert "::/BM/BCM43430A1.HCD" in ls("bm.img").upper(), ls("bm.img")
        assert "::/BM/SYN43430B0.HCD" in ls("bm.img").upper(), ls("bm.img")
        assert "::/KERNEL.IMG" in ls("bm.img").upper(), ls("bm.img")
        assert ("::/KERNEL7.IMG" in ls("bm.img").upper()) == ZERO2, ls("bm.img")
        # the carts nano8 plays, with their long names
        assert "::/carts/nano8/nanodemo.p8" in ls("bm.img"), ls("bm.img")
        assert "::/carts/nano8/starmoovalley.p8.png" in ls("bm.img"), ls("bm.img")
        pi1 = ls("bm-pi1.img")
        assert "::/KERNEL.IMG" in pi1.upper() and "BCM43430A1" not in pi1.upper(), pi1
        assert "KERNEL7" not in pi1.upper(), pi1
        q = Qemu(os.path.join(os.path.dirname(b("kernel.img")), "kernel.img"),   # kernel.img even with --kernel7
                 ["-drive", f"if=sd,format=raw,file={os.path.join(tmp, 'bm-pi1.img')}"], machine="raspi1ap")
        out = q.expect(MENU, timeout=30).decode(errors="replace")
        assert "Raspberry Pi 1 A+" in out and f"; {len(games)} cartridges" in out, out
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


WRITER_CART = r"""
function _init()
  local code = { lua = "function _draw() cls(0) end", title = "written" }
  for _, p in ipairs({ "/kernel.img", "/bm/config.txt", "/carts/../kernel.bm", "/carts/sub/x.bm",
                       "/carts/.x.bm", "notes.txt", "/carts/ok.bm", "ok2.bm" }) do
    local ok, err = cart_save(p, code)
    log("save", p, ok, err)
  end
  local ok, err = cart_write("/kernel.img", { lua = "x" , from = "/carts/ok.bm" })
  log("write", ok, err)
  ok, err = cart_put_audio("/bm/config.txt", nil)
  log("audio", ok, err)
  quit()
end
"""


def test_cart_write_limits(b, opts):
    """M25 (Market): a cartridge from the SD card or the Market writes only
    .bm files in /carts; the kernel, the settings and other folders are
    refused (the tools built into the kernel still save anywhere)."""
    tmp = tempfile.mkdtemp(prefix="bm-wlim-")
    img = os.path.join(tmp, "sd.img")
    cfg = os.path.join(tmp, "config.txt")
    with open(cfg, "w") as f:
        f.write("layout=us\nwifi_boot=0\n")
    mksd.build(img, [(cfg, "bm/config.txt")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    try:
        q.boot()
        assert _upload(q, mkbm.pack(WRITER_CART.encode(), title="writer"))
        out = q.expect("audio\t", timeout=20)
        out += q.expect("\n")
        text = out.decode(errors="replace").replace("\r", "")
        for p in ("/kernel.img", "/bm/config.txt", "/carts/../kernel.bm", "/carts/sub/x.bm",
                  "/carts/.x.bm", "notes.txt"):
            assert f"save\t{p}\tfalse\t{p}: a cartridge writes only .bm files in /carts" in text, text
        assert "save\t/carts/ok.bm\ttrue" in text, text
        assert "save\tok2.bm\ttrue" in text, text
        assert "write\tfalse\t/kernel.img: a cartridge writes only" in text, text
        assert "audio\tfalse\t/bm/config.txt: a cartridge writes only" in text, text
        q.expect("> ", timeout=10)
    finally:
        q.close()
    try:
        part = os.path.join(tmp, "part.img")
        with open(img, "rb") as f, open(part, "wb") as o:
            f.seek(2048 * 512)
            o.write(f.read())
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
        root = subprocess.run(["mdir", "-b", "-i", part, "::/"], capture_output=True,
                              text=True, env=env).stdout
        assert "KERNEL.IMG" not in root.upper() and "NOTES.TXT" not in root.upper(), root
        cart_dir = subprocess.run(["mdir", "-b", "-i", part, "::/CARTS"], capture_output=True,
                                  text=True, env=env).stdout.upper()
        assert "OK.BM" in cart_dir and "OK2.BM" in cart_dir, cart_dir
        cfg_txt = subprocess.run(["mtype", "-i", part, "::/BM/CONFIG.TXT"], capture_output=True,
                                 text=True, env=env).stdout
        assert "wifi_boot=0" in cfg_txt, cfg_txt
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

    def report(self, buttons=0x08, ps=0, handle=None, lx=128, ly=128, shoulders=0, rx=128, ry=128):
        """DS4 reduced input report 0x01 on the host's interrupt channel;
        shoulders: 1 = L1, 2 = R1, 4 = L2, 8 = R2."""
        self.l2(0x0041, bytes([0xA1, 0x01, lx, ly, rx, ry, buttons, shoulders, ps, 0, 0]), handle)


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
            _mini_expect(q, "bm: loaded")
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
            # the pad drops: the log says why (the report of the log is all
            # the user has to go on)
            chip._event(0x05, bytes([0]) + chip.HANDLE.to_bytes(2, "little") + bytes([0x08]))
            _mini_expect(q, "bt: controller 1c:66:6d:01:02:03 (player 1) disconnected after ")
            _mini_expect(q, "radio link lost (distance, WiFi, battery) (reason 08)")
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
        self.frames, self.rx = [], {}                       # by LE handle
        self.handles = {self.LE_HANDLE}
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
    def _next(self, want_cmd=None, want_cid=None, timeout=60, handle=None):
        """Answers commands and collects L2CAP frames until the command
        `want_cmd` or a frame on `want_cid` (of the link `handle`) comes;
        returns its parameters."""
        handle = handle or self.LE_HANDLE
        deadline = time.time() + timeout
        while time.time() < deadline:
            for i, (h, cid, data) in enumerate(self.frames):
                if cid == want_cid and h == handle:
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
                (h, pb), data = a, payload
                rx = data if pb != 1 else self.rx.get(h, b"") + data
                self.rx[h] = rx
                if len(rx) >= 4 and len(rx) >= 4 + int.from_bytes(rx[:2], "little"):
                    n = int.from_bytes(rx[:2], "little")
                    self.frames.append((h, int.from_bytes(rx[2:4], "little"), rx[4:4 + n]))
                    self.rx[h] = b""
        raise AssertionError(f"fake keyboard: timeout waiting for {want_cmd or want_cid:#x}")

    def packet_any(self):
        t = self._read(1, timeout=60)[0]
        if t == 0x01:
            op = int.from_bytes(self._read(2), "little")
            return "cmd", op, self._read(self._read(1)[0])
        assert t == 0x02, f"fake chip: packet type {t:#x}"
        hdr = self._read(4)
        h = int.from_bytes(hdr[:2], "little")
        assert h & 0x0FFF in self.handles, f"ACL on handle {h & 0xFFF:#x}"
        data = self._read(int.from_bytes(hdr[2:4], "little"))
        assert len(data) <= 27, f"host sent {len(data)} bytes in one packet (buffers are 27)"
        return "acl", (h & 0x0FFF, (h >> 12) & 3), data

    def send_l2(self, cid, data, piece=None, handle=None):
        frame = len(data).to_bytes(2, "little") + cid.to_bytes(2, "little") + data
        piece = piece or len(frame)
        for off in range(0, len(frame), piece):
            flag = 0x2000 if off == 0 else 0x1000
            chunk = frame[off:off + piece]
            self.port.write(bytes([0x02]) + ((handle or self.LE_HANDLE) | flag).to_bytes(2, "little")
                            + len(chunk).to_bytes(2, "little") + chunk)

    def le_meta(self, sub, params):
        self._event(0x3E, bytes([sub]) + params)

    def advertise(self, addr, addr_type, data):
        self.le_meta(0x02, bytes([1, 0x00, addr_type]) + addr + bytes([len(data)]) + data + bytes([0xC8]))

    def connect(self, addr, addr_type, handle=None):
        p = self._next(want_cmd=0x200D)
        assert p[5] == addr_type and p[6:12] == addr, p.hex()
        self.le_meta(0x01, bytes([0]) + (handle or self.LE_HANDLE).to_bytes(2, "little") + bytes([0, addr_type])
                     + addr + bytes([0x0C, 0, 0, 0, 0xC8, 0, 0]))

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

    def serve_gatt(self, db=None, svc=(0x10, None), cccd=0x17, handle=None):
        """Answers the host's GATT client until it turns notifications on
        (on `cccd`); db: the attributes of the HID service svc (start, end)."""
        db = db or self.db
        start_, end_ = svc[0], svc[1] or self.svc_end

        def send(data):
            self.send_l2(4, data, handle=handle)
        while True:
            req = self._next(want_cid=4, handle=handle)
            op = req[0]
            if op == 0x06:                                           # find by type value
                assert req[5:9] == bytes([0x00, 0x28, 0x12, 0x18]), req.hex()
                send(bytes([0x07]) + start_.to_bytes(2, "little") + end_.to_bytes(2, "little"))
            elif op == 0x08:                                         # read by type (chars)
                start, end = int.from_bytes(req[1:3], "little"), int.from_bytes(req[3:5], "little")
                hs = [h for h in sorted(db) if start <= h <= end and db[h][0] == 0x2803][:3]
                if not hs:
                    send(bytes([0x01, 0x08]) + req[1:3] + bytes([0x0A]))
                else:
                    send(bytes([0x09, 7]) + b"".join(h.to_bytes(2, "little") + db[h][1] for h in hs))
            elif op == 0x04:                                         # find information
                start, end = int.from_bytes(req[1:3], "little"), int.from_bytes(req[3:5], "little")
                hs = [h for h in sorted(db) if start <= h <= end][:5]
                if not hs:
                    send(bytes([0x01, 0x04]) + req[1:3] + bytes([0x0A]))
                else:
                    send(bytes([0x05, 1]) + b"".join(
                        h.to_bytes(2, "little") + db[h][0].to_bytes(2, "little") for h in hs))
            elif op in (0x0A, 0x0C):                                 # read, read blob
                h = int.from_bytes(req[1:3], "little")
                off = int.from_bytes(req[3:5], "little") if op == 0x0C else 0
                value = db[h][1]
                if off > len(value):
                    send(bytes([0x01, op]) + req[1:3] + bytes([0x07]))
                else:
                    send(bytes([op + 1]) + value[off:off + 22])
            elif op == 0x12:                                         # write request
                h = int.from_bytes(req[1:3], "little")
                assert h == cccd and req[3:5] == bytes([1, 0]), f"notifications on {h:#x}"
                send(bytes([0x13]))
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


class FakeMxMouse(FakeMxKeys):
    """The keyboard of FakeMxKeys and an LE mouse beside it (like a Logitech
    MX Master): it cannot type, so the pairing is Just Works (LE Secure
    Connections), its identity key; a HID service with the mouse in report
    ID 2 (16 buttons, X and Y of 12 bits, wheel, AC Pan) and the boot mouse
    report; motion as notifications. Both links at the same time."""

    MOUSE_LE = bytes([0x65, 0x43, 0x21, 0x0F, 0xED, 0xDC | 0xC0])     # static random
    MOUSE_ID = bytes([0x11, 0x22, 0x33, 0x9E, 0x6D, 0x00])            # 00:6d:9e:33:22:11 public
    MOUSE_IRK = bytes(range(0x50, 0x60))
    MOUSE_HANDLE = 0x0041
    MOUSE_MAP = bytes([
        0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x02, 0x09, 0x01, 0xA1, 0x00,
        0x05, 0x09, 0x19, 0x01, 0x29, 0x10, 0x15, 0x00, 0x25, 0x01, 0x95, 0x10, 0x75, 0x01, 0x81, 0x02,
        0x05, 0x01, 0x16, 0x01, 0xF8, 0x26, 0xFF, 0x07, 0x75, 0x0C, 0x95, 0x02, 0x09, 0x30, 0x09, 0x31,
        0x81, 0x06, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x01, 0x09, 0x38, 0x81, 0x06,
        0x05, 0x0C, 0x0A, 0x38, 0x02, 0x95, 0x01, 0x81, 0x06, 0xC0, 0xC0,
        0x06, 0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x10, 0x75, 0x08, 0x95, 0x06, 0x15, 0x00,
        0x26, 0xFF, 0x00, 0x09, 0x01, 0x81, 0x00, 0x09, 0x01, 0x91, 0x00, 0xC0])  # vendor (HID++)

    def __init__(self, port):
        super().__init__(port)
        self.handles.add(self.MOUSE_HANDLE)
        db = {0x30: (0x2800, (0x1812).to_bytes(2, "little"))}
        chars = [(0x31, 0x02, 0x2A4A, bytes([0x11, 0x01, 0x00, 0x02])),
                 (0x33, 0x02, 0x2A4B, self.MOUSE_MAP),
                 (0x35, 0x12, 0x2A4D, bytes(7)), (0x39, 0x12, 0x2A33, bytes(3)),
                 (0x3C, 0x04, 0x2A4C, bytes(1)), (0x3E, 0x06, 0x2A4E, bytes([1]))]
        for h, props, uuid, value in chars:
            db[h] = (0x2803, bytes([props]) + (h + 1).to_bytes(2, "little") + uuid.to_bytes(2, "little"))
            db[h + 1] = (uuid, value)
        db.update({0x37: (0x2902, bytes(2)), 0x38: (0x2908, bytes([2, 1])), 0x3B: (0x2902, bytes(2))})
        self.mouse_db = db

    def pair_mouse(self, host_addr):
        """Advertises as a mouse in pairing mode, is connected, pairs with
        Just Works (SC), gives its IRK and identity address."""
        from Crypto.PublicKey import ECC
        h = self.MOUSE_HANDLE
        self._next(want_cmd=0x200B)
        self._next(want_cmd=0x200C)
        adv = bytes([2, 0x01, 0x05, 3, 0x19, 0xC2, 0x03, 3, 0x03, 0x12, 0x18, 13, 0x09]) + b"MX Master 3S"
        self.advertise(self.MOUSE_LE, 1, adv)
        self.connect(self.MOUSE_LE, 1, handle=h)
        preq = self._next(want_cid=6, handle=h)
        assert preq[0] == 0x01, preq.hex()
        pres = bytes([0x02, 0x03, 0x00, 0x09, 16, 0x00, 0x02])       # NoInputNoOutput, SC, bonding
        self.send_l2(6, pres, handle=h)
        pk = self._next(want_cid=6, handle=h)
        assert pk[0] == 0x0C and len(pk) == 65, pk.hex()
        pkax = pk[1:33]
        host_pub = ECC.construct(curve="P-256", point_x=int.from_bytes(pk[1:33], "little"),
                                 point_y=int.from_bytes(pk[33:65], "little"))
        key = ECC.generate(curve="P-256")
        pkbx = int(key.pointQ.x).to_bytes(32, "little")
        pkby = int(key.pointQ.y).to_bytes(32, "little")
        self.send_l2(6, bytes([0x0C]) + pkbx + pkby, piece=27, handle=h)
        dh = int((host_pub.pointQ * key.d).x).to_bytes(32, "little")
        nb = os.urandom(16)                                     # Just Works: we confirm first
        self.send_l2(6, bytes([0x03]) + self.f4(pkbx, pkax, nb, 0), handle=h)
        na = self._next(want_cid=6, handle=h)
        assert na[0] == 0x04, na.hex()
        self.send_l2(6, bytes([0x04]) + nb, handle=h)
        a = host_addr + bytes([0])
        b_ = self.MOUSE_LE + bytes([1])
        mackey, ltk = self.f5(dh, na[1:], nb, a, b_)
        ea = self._next(want_cid=6, handle=h)
        assert ea[0] == 0x0D and ea[1:] == self.f6(mackey, na[1:], nb, bytes(16), preq[1:4], a, b_), "bad Ea"
        self.send_l2(6, bytes([0x0D]) + self.f6(mackey, nb, na[1:], bytes(16), pres[1:4], b_, a), handle=h)
        enc = self._next(want_cmd=0x2019)
        assert enc[:2] == h.to_bytes(2, "little") and enc[12:28] == ltk, "host encrypts with another key"
        self._event(0x08, bytes([0]) + h.to_bytes(2, "little") + bytes([1]))
        self.send_l2(6, bytes([0x08]) + self.MOUSE_IRK, handle=h)
        self.send_l2(6, bytes([0x09, 0]) + self.MOUSE_ID, handle=h)
        self.mouse_ltk = ltk

    def serve_mouse(self):
        self.serve_gatt(self.mouse_db, (0x30, 0x3F), 0x37, self.MOUSE_HANDLE)

    def move(self, dx=0, dy=0, buttons=0, wheel=0):
        """A mouse report (ID 2): 16 buttons, X and Y of 12 bits, wheel, pan."""
        xy = (dx & 0xFFF) | (dy & 0xFFF) << 12
        rep = buttons.to_bytes(2, "little") + xy.to_bytes(3, "little") + bytes([wheel & 0xFF, 0])
        self.send_l2(4, bytes([0x1B, 0x36, 0x00]) + rep, handle=self.MOUSE_HANDLE)

    def mouse_come_back(self):
        """The mouse drops its link, then advertises from a resolvable private
        address: the host connects and encrypts with the saved key."""
        h = self.MOUSE_HANDLE
        self._event(0x05, bytes([0]) + h.to_bytes(2, "little") + bytes([0x08]))
        self._next(want_cmd=0x200B)
        self._next(want_cmd=0x200C)
        prand = bytes([0x44, 0x55, 0x40 | 0x26])
        rpa = self.ah(self.MOUSE_IRK, prand) + prand
        self.advertise(rpa, 1, bytes([2, 0x01, 0x04]))
        self.connect(rpa, 1, handle=h)
        enc = self._next(want_cmd=0x2019)
        assert enc[12:28] == self.mouse_ltk and enc[2:12] == bytes(10), "saved key not used"
        self._event(0x08, bytes([0]) + h.to_bytes(2, "little") + bytes([1]))


def test_bt_mouse(b, opts):
    """M32 with a simulated MX Keys and MX Master: the keyboard is paired
    ('K'), then the mouse ('O': Just Works, no code) while the keyboard
    stays connected; the bar shows the keyboard (blue 1) and a mouse with a
    blue dot; the mouse moves the pointer over the covers and plays one with
    a click; Settings > Controllers says so; the keyboard still types. The
    mouse comes back from a private address (its IRK) and moves the pointer
    again; both bonds are in bm/config.txt."""
    try:
        import Crypto  # noqa: F401
    except ImportError:
        print("    skipped: pip install pycryptodome")
        return
    import re
    tmp = tempfile.mkdtemp(prefix="bm-btmouse-")
    img = os.path.join(tmp, "sd.img")
    hcd = os.path.join(tmp, "BCM43430A1.hcd")
    with open(hcd, "wb") as f:
        f.write(bytes([0x4C, 0xFC, 4, 1, 2, 3, 4, 0x4E, 0xFC, 4, 0xFF, 0xFF, 0xFF, 0xFF]))
    mksd.build(img, [(hcd, "bm/BCM43430A1.hcd"), (b("carts/pong.bm"), "carts/pong.bm"),
                     (b("carts/snake.bm"), "carts/snake.bm")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"], mini_uart=True)
    q.mini_buf = b""
    try:
        q.boot()
        q.send("K")
        q.expect("(same pins, same speed)\r\n")
        chip = FakeMxMouse(q.port)
        chip.buf, q.buf = q.buf, b""
        chip.init(reset_silent=False)

        def passkey():
            _mini_expect(q, "then Enter:")
            text, deadline = "", time.time() + 10
            while not re.search(r"\b\d{6}\b", text) and time.time() < deadline:
                q.mini_buf += q.mini.read(0.05)
                text = q.mini_buf.decode(errors="replace")
            return int(re.search(r"\b(\d{6})\b", text).group(1))
        chip.pair(FakeBtChip.ADDR, passkey)
        chip.serve_gatt()
        _mini_expect(q, "ready to type")
        # the mouse, while the keyboard stays connected
        time.sleep(0.3)
        q.mini.write(b"O")
        try:
            chip.pair_mouse(FakeBtChip.ADDR)
            chip.serve_mouse()
        except AssertionError:
            q.mini_buf += q.mini.read(0.5)
            print(q.mini_buf.decode(errors="replace")[-3000:])
            raise
        out = _mini_expect(q, "it moves the pointer")
        for s_ in ("found mouse dc:ed:0f:21:43:65 MX Master 3S (random address)",
                   "pairing, LE Secure Connections, no code (Just Works)",
                   "mouse 00:6d:9e:33:22:11 paired (Secure Connections, private address)",
                   f"report map {len(FakeMxMouse.MOUSE_MAP)} bytes: mouse found, report ID 2",
                   "mouse MX Master 3S connected"):
            assert s_ in out, out
        # the keyboard still types: 'i' (info)
        time.sleep(0.3)
        chip.keys(0x0C)
        chip.keys()
        _mini_expect(q, "uptime")
        # the menu: keyboard (blue 1) and the mouse (blue dot) in the bar
        q.mini.write(b"M")
        _mini_expect(q, "cartridge menu")
        shot_ = wait_icons(q, 2)
        runs = bar_icons(shot_)
        assert len(runs) == 2 and blue_number(shot_, runs[0]) and blue_number(shot_, runs[1]), runs
        assert arrow_at(shot_, 320, 180), "no arrow"
        # to the top left corner, then over the covers: 1.2 pixels per count
        # when moving fast
        chip.move(-2000, -2000)
        assert arrow_at(wait_screen(q, lambda s_: arrow_at(s_, 0, 0)), 0, 0), "not in the corner"
        chip.move(55, 120)                               # (66, 144): Pong
        shot_ = wait_screen(q, lambda s_: arrow_at(s_, 66, 144) and title_is("Pong")(s_))
        assert arrow_at(shot_, 66, 144) and "Pong" in screen_text(shot_)[4], screen_text(shot_)[4]
        chip.move(120, 0)                                # (210, 144): Snake
        assert "Snake" in screen_text(wait_screen(q, title_is("Snake")))[4]
        # Settings > Controllers: the mouse is there
        q.mini.write(b"5")
        wait_screen(q, lambda s_: "Controllers" in "".join(screen_text(s_)))
        q.mini.write(b"\r")
        text = "\n".join(screen_text(wait_screen(q, lambda s_: "Bluetooth on" in "".join(screen_text(s_)))))
        assert re.search(r"Mouse +Bluetooth on", text), text
        q.mini.write(b"\x1b")                             # back to Settings, to Games
        time.sleep(0.5)
        q.mini.write(b"2")
        shot_ = wait_screen(q, lambda s_: title_is("Snake")(s_) and arrow_at(s_, 210, 144))
        assert arrow_at(shot_, 210, 144), "the arrow is not on Snake"
        chip.move(0, 0, buttons=1)                      # a click on Snake
        time.sleep(0.1)
        chip.move(0, 0, buttons=0)
        _mini_expect(q, "playing snake.bm")
        _mini_expect(q, "bm: loaded")
        time.sleep(0.5)
        q.mini.write(b"q")
        _mini_expect(q, "update+draw")
        time.sleep(0.5)
        # it comes back: found by its IRK, the saved key; it moves again
        chip.mouse_come_back()
        chip.serve_mouse()
        _mini_expect(q, "mouse MX Master 3S connected")
        time.sleep(0.5)
        chip.move(-2000, -2000)
        wait_screen(q, lambda s_: arrow_at(s_, 0, 0))
        chip.move(55, 120)
        shot_ = wait_screen(q, lambda s_: arrow_at(s_, 66, 144) and title_is("Pong")(s_))
        assert arrow_at(shot_, 66, 144) and "Pong" in screen_text(shot_)[4], screen_text(shot_)[4]
    finally:
        q.close()
    part = os.path.join(tmp, "part.img")
    with open(img, "rb") as f, open(part, "wb") as o:
        f.seek(2048 * 512)
        o.write(f.read())
    cfg = subprocess.run(["mtype", "-i", part, "::/BM/CONFIG.TXT"], capture_output=True, text=True,
                         env=dict(os.environ, MTOOLS_SKIP_CHECK="1")).stdout
    shutil.rmtree(tmp, ignore_errors=True)
    assert "bt_mouse=00:6d:9e:33:22:11 0 " + FakeMxMouse.MOUSE_IRK.hex() in cfg, cfg
    assert "bt_mouse_key=" + chip.mouse_ltk.hex() + " 0000 0000000000000000" in cfg, cfg
    assert "bt_kbd=00:6d:9e:12:34:56 0 " in cfg, cfg


class FakeClassicMouse(FakeDs4Chip):
    """The chip plus a classic Bluetooth mouse (class 002580): bm looks for
    an LE mouse first (nothing), then for a classic one: the inquiry finds
    it, SSP Just Works, encryption, the HID channels; bm switches it to the
    boot protocol (SET_PROTOCOL on the control channel), its reports are
    A1 02 buttons X Y wheel."""

    MOUSE = bytes([0x0C, 0x0B, 0x0A, 0x6D, 0x66, 0x1C])         # 1c:66:6d:0a:0b:0c

    def pair_mouse(self, key, handle, cids, clock=(0x21, 0x43)):
        while True:                                         # the LE scan: nobody
            deadline = time.time() + 30                     # up to 11.5 s of nothing
            while not self.buf and time.time() < deadline:
                self.buf += self.port.read(0.05)
            kind, op, params = self.packet()
            assert kind == "cmd", kind
            if op == 0x0401:
                break
            self._complete(op)
        self.status(0x0401)
        self._event(0x22, bytes([1]) + self.MOUSE + bytes([1, 0, 0x80, 0x25, 0x00, *clock, 0xC4]))
        self._event(0x01, bytes([0]))
        p = self.cmd(0x0405, reply="status")
        assert p[:6] == self.MOUSE, p.hex()
        hb = handle.to_bytes(2, "little")
        self._event(0x03, bytes([0]) + hb + self.MOUSE + bytes([1, 0]))
        self.cmd(0x0411, reply="status")
        self._event(0x17, self.MOUSE)
        self.cmd(0x040C)
        self._event(0x31, self.MOUSE)
        io = self.cmd(0x042B)
        assert io == self.MOUSE + bytes([0x03, 0x00, 0x04]), io.hex()
        self._event(0x33, self.MOUSE + (654321).to_bytes(4, "little"))
        self.cmd(0x042C)
        self._event(0x36, bytes([0]) + self.MOUSE)
        self._event(0x18, self.MOUSE + key + bytes([4]))
        self._event(0x06, bytes([0]) + hb)
        self.cmd(0x0413, reply="status")
        self._event(0x08, bytes([0]) + hb + bytes([1]))
        for psm, host_cid, dev_cid in ((0x11, 0x40, cids[0]), (0x13, 0x41, cids[1])):
            code, ident, data = self.host_sig(handle)
            assert code == 0x02 and data == psm.to_bytes(2, "little") + host_cid.to_bytes(2, "little")
            self.sig(0x03, ident, dev_cid.to_bytes(2, "little") + host_cid.to_bytes(2, "little") + bytes(4),
                     handle)
            self.configure(dev_cid, host_cid, handle)
        kind, h, payload = self.packet()                    # SET_PROTOCOL (boot)
        assert kind == "acl" and h == handle and int.from_bytes(payload[2:4], "little") == cids[0] \
            and payload[4:] == bytes([0x70]), payload.hex()
        self.l2(0x0040, bytes([0x00]), handle)              # HANDSHAKE: successful

    def move(self, dx=0, dy=0, buttons=0, wheel=0):
        self.l2(0x0041, bytes([0xA1, 0x02, buttons, dx & 0xFF, dy & 0xFF, wheel & 0xFF]))


def test_bt_mouse_classic(b, opts):
    """M32: a classic Bluetooth mouse ('O' finds no LE mouse, then pairs a
    classic one: boot protocol). Alone in the bar: the mouse with a blue
    dot; it moves the pointer and plays a cover with a click; its key is
    bt_mouse_classic in bm/config.txt."""
    tmp = tempfile.mkdtemp(prefix="bm-btmouse2-")
    img = os.path.join(tmp, "sd.img")
    hcd = os.path.join(tmp, "BCM43430A1.hcd")
    with open(hcd, "wb") as f:
        f.write(bytes([0x4C, 0xFC, 4, 1, 2, 3, 4, 0x4E, 0xFC, 4, 0xFF, 0xFF, 0xFF, 0xFF]))
    mksd.build(img, [(hcd, "bm/BCM43430A1.hcd"), (b("carts/pong.bm"), "carts/pong.bm"),
                     (b("carts/snake.bm"), "carts/snake.bm")])
    key = bytes(range(0xC0, 0xD0))
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"], mini_uart=True)
    q.mini_buf = b""
    try:
        q.boot()
        q.send("O")
        q.expect("(same pins, same speed)\r\n")
        chip = FakeClassicMouse(q.port)
        chip.buf, q.buf = q.buf, b""
        chip.init(reset_silent=False)
        chip.pair_mouse(key, chip.HANDLE, (0x60, 0x61))
        out = _mini_expect(q, "mouse paired; next time click it to connect", timeout=40)
        for s_ in ("bt: no mouse in pairing mode found",
                   "bt: found 1c:66:6d:0a:0b:0c class 002580",
                   "bt: mouse 1c:66:6d:0a:0b:0c connected, it moves the pointer"):
            assert s_ in out, out
        q.mini.write(b"M")
        _mini_expect(q, "cartridge menu")
        shot_ = wait_icons(q, 1)
        runs = bar_icons(shot_)
        assert len(runs) == 1 and blue_number(shot_, runs[0]) and arrow_at(shot_, 320, 180), runs
        for _ in range(3):                              # to the corner (8-bit motion)
            chip.move(-127, -127)
            time.sleep(0.2)
        wait_screen(q, lambda s_: arrow_at(s_, 0, 0))
        chip.move(55, 120)                              # (66, 144): Pong
        shot_ = wait_screen(q, lambda s_: arrow_at(s_, 66, 144) and title_is("Pong")(s_))
        assert arrow_at(shot_, 66, 144) and "Pong" in screen_text(shot_)[4], screen_text(shot_)[4]
        chip.move(buttons=1)
        time.sleep(0.1)
        chip.move()
        _mini_expect(q, "playing pong.bm")
    finally:
        q.close()
    part = os.path.join(tmp, "part.img")
    with open(img, "rb") as f, open(part, "wb") as o:
        f.seek(2048 * 512)
        o.write(f.read())
    cfg = subprocess.run(["mtype", "-i", part, "::/BM/CONFIG.TXT"], capture_output=True, text=True,
                         env=dict(os.environ, MTOOLS_SKIP_CHECK="1")).stdout
    shutil.rmtree(tmp, ignore_errors=True)
    assert "bt_mouse_classic=1c:66:6d:0a:0b:0c " + key.hex() in cfg, cfg


def test_stick_pointer(b, opts):
    """M32: without a mouse the right stick of a pad moves the pointer: in
    the menu the arrow shows only once the stick moves; in a cartridge
    that asks for the pointer, R2 is its left button and L2 the right one."""
    tmp = tempfile.mkdtemp(prefix="bm-stick-")
    img = os.path.join(tmp, "sd.img")
    hcd = os.path.join(tmp, "BCM43430A1.hcd")
    with open(hcd, "wb") as f:
        f.write(bytes([0x4C, 0xFC, 4, 1, 2, 3, 4, 0x4E, 0xFC, 4, 0xFF, 0xFF, 0xFF, 0xFF]))
    cart = os.path.join(tmp, "mouse.bm")
    with open(cart, "wb") as f:
        f.write(mkbm.pack(MOUSE_CART.encode(), title="Mouse test", res=(320, 180)))
    mksd.build(img, [(hcd, "bm/BCM43430A1.hcd"), (cart, "carts/mouse.bm")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"], mini_uart=True)
    q.mini_buf = b""
    try:
        q.boot()
        q.send("T")
        q.expect("(same pins, same speed)\r\n")
        chip = FakeDs4Chip(q.port)
        chip.buf, q.buf = q.buf, b""
        chip.init(reset_silent=False)
        chip.pair(FakeDs4Chip.DS4, chip.KEY, chip.HANDLE, (0x70, 0x71), 1)
        _mini_expect(q, "next time just press PS")
        q.mini.write(b"M")
        _mini_expect(q, "cartridge menu")
        assert not arrow_at(wait_icons(q, 1), 320, 180), "an arrow before the stick moved"
        chip.report(rx=255)                             # right, for a moment
        time.sleep(0.3)
        chip.report()
        time.sleep(0.3)
        shot_ = q.screendump()
        xs = [x for x in range(321, 636) if arrow_at(shot_, x, 180)]
        assert xs, "the arrow did not show up to the right"
        chip.report(0x08 | 0x20)                        # cross: plays the cover
        time.sleep(0.1)
        chip.report()
        _mini_expect(q, "playing mouse.bm")
        out = _mini_expect(q, "w0 true")
        x0 = int(re.search(r"mouse (\d+),", out).group(1))
        chip.report(rx=0)                               # left
        time.sleep(0.3)
        chip.report()
        out = _mini_expect(q, "w0 true")
        time.sleep(0.3)
        out += q.mini.read(0.2).decode(errors="replace")
        xs = [int(v) for v in re.findall(r"mouse (\d+),", out)]
        assert xs and min(xs) < x0, (x0, out)
        chip.report(shoulders=0x08)                     # R2: the left button
        time.sleep(0.1)
        chip.report()
        _mini_expect(q, " click")
        chip.report(shoulders=0x04)                     # L2: the right one
        time.sleep(0.1)
        chip.report()
        _mini_expect(q, " right")
        chip.report(0x08, ps=1)
        _mini_expect(q, "update+draw")
    finally:
        q.close()
        shutil.rmtree(tmp, ignore_errors=True)


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
        shot_, runs = wait_bar_icons(
            q, lambda r, i: len(r) == 2 and blue_number(i, r[1]))
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
        shot_, runs = wait_bar_icons(q, lambda r, i: len(r) == 1 and blue_number(i, r[0]))
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
    """The tabs with a DS4 (M27): R1 and L1 move between Market, Games, Dev,
    Lib and Settings (the menu opens on Games); on Settings its panel opens by
    itself and Lib is off; B out of it goes back to Lib. Up on the first row
    stays on the covers. PS in the
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
        press(shoulders=2)                  # R1: Lib
        state(["Lib"], ["Models", "Images"])
        press(shoulders=2)                  # R1: Settings, its panel open, Lib off
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
        press(buttons=0x00)                 # up three times: round to Button icons
        press(buttons=0x00)
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
        press(buttons=0x08 | 0x40)          # B (circle): out of Settings, back to Lib
        state(["Lib"], ["Models"], gone=["Controllers"])
        press(shoulders=1)                  # L1: Dev
        state(["Dev"], ["bm SDK"])
        press(shoulders=1)                  # L1: Games
        state(["Games"], ["bm native demo"])
        press(shoulders=1)                  # L1: the Market, first (M25): no key in this kernel
        state(["Market"], ["The Market needs a key"])
        press(shoulders=1)                  # L1 on the first tab: nothing
        state(["Market"], ["The Market needs a key"])
        press(shoulders=2)                  # R1: back to Games
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


MARKET_GAME = r"""
function _init() log("market game runs") quit() end
"""


def _market_site(tmp, b, key):
    """A market on the SD card (market_url=sd:/market/): four games signed
    with a test key, one of them (Broken) changed after signing."""
    games = os.path.join(tmp, "games")
    cover = (128, 80, bytes([200, 60, 40, 255]) * (128 * 80))       # one red
    carts = [("mtest", "mtest.bm", mkbm.pack(MARKET_GAME.encode(), title="Market Test", author="tests",
                                             cover=cover)),
             ("pong", "pong.bm", open(b("carts/pong.bm"), "rb").read()),
             ("snake", "snake.bm", open(b("carts/snake.bm"), "rb").read()),
             ("zbroken", "broken.bm", mkbm.pack(MARKET_GAME.encode(), title="Broken", author="tests"))]
    for gid, name, data in carts:
        d = os.path.join(games, gid)
        os.makedirs(d)
        with open(os.path.join(d, name), "wb") as f:
            f.write(data)
        with open(os.path.join(d, "info.txt"), "w") as f:
            f.write("version: 1.0\nlicense: MIT\nabout: A game for the test.\n")
    site = os.path.join(tmp, "site")
    r = subprocess.run([sys.executable, os.path.join(HERE, "..", "scripts", "mkmarket.py"), games, "-o", site,
                        "--key", key, "--serial", "20261001120000"], capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
    broken = os.path.join(site, "games", "zbroken", "broken.bm")
    data = bytearray(open(broken, "rb").read())
    data[-1] ^= 1                                       # not the file of the catalog any more
    with open(broken, "wb") as f:
        f.write(data)
    files = []
    for dp, _, fs in os.walk(site):
        for f in fs:
            src = os.path.join(dp, f)
            files.append((src, "market/" + os.path.relpath(src, site).replace(os.sep, "/")))
    return files


def test_market(b, opts):
    """M25: the Market tab, first in the menu, with a catalog in a folder of
    the SD card (slowed down by market_delay, as a network would be): the
    tab shows placeholders at once, the catalog and the covers arrive while
    the menu runs, nothing loads while another tab is shown, a game
    downloads with its progress on the cover, is checked, installed in
    /carts and plays; a game already on the card shows as installed; a file
    that is not the catalog's is refused; leaving the tab interrupts a
    download."""
    tmp = tempfile.mkdtemp(prefix="bm-market-")
    try:
        key = os.path.join(tmp, "key.pem")
        pub = os.path.join(tmp, "market.pem")
        subprocess.run(["openssl", "genpkey", "-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-256",
                        "-out", key], check=True, capture_output=True)
        subprocess.run(["openssl", "pkey", "-in", key, "-pubout", "-out", pub], check=True, capture_output=True)
        cfg = os.path.join(tmp, "config.txt")
        with open(cfg, "w") as f:
            f.write("layout=us\nwifi_boot=0\nmarket_url=sd:/market/\nmarket_delay=250\n")
        img = os.path.join(tmp, "sd.img")
        mksd.build(img, [(cfg, "bm/config.txt"), (pub, "bm/market.pem"), (b("carts/pong.bm"), "carts/pong.bm")]
                   + _market_site(tmp, b, key))
        q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])

        def keys(k):
            for c in k:
                q.send(c)
                time.sleep(0.25)

        def screen(want, gone=(), tries=30):
            for _ in range(tries):
                _, text = settled_screen(q, lambda i, t: True, tries=1)
                joined = "\n".join(text)
                if all(w in joined for w in want) and not any(g in joined for g in gone):
                    return joined
                time.sleep(0.2)
            raise AssertionError(f"want {want}, not {gone}, on the screen:\n{joined}")

        def shot(name):
            if opts.shots:
                _save_png(q.screendump(), os.path.join(opts.shots, f"market-{name}.png"))

        def is_red(img, x, y):
            r, g_, bl = pixel(img, x, y)
            return r > 150 and g_ < 100 and bl < 80

        try:
            q.expect(MENU, timeout=30)
            time.sleep(0.5)
            img_, _ = settled_screen(q, lambda i, t: tabs_lit(i) == ["Games"])
            assert tabs_lit(img_) == ["Games"], "the menu opens on Games"
            time.sleep(1.0)
            assert b"market:" not in q.buf, "nothing loads before the tab is shown"

            keys("1")                                   # the Market tab: placeholders first
            screen(["Loading the Market..."])
            lit = img_tabs(q, ["Market"])       # the pills slide in: a frame may be half drawn
            assert lit == ["Market"], lit
            shot("loading")
            q.expect("market: sd:/market/, no catalog saved", timeout=10)
            q.expect("market: catalog 20261001120000, 4 games", timeout=15)
            # the title on a placeholder while the covers come one at a time,
            # nearest the selection first (Snake's is the third)
            screen(["Snake"], tries=10)
            shot("placeholders")
            screen(["4 games, 2026-10-01"])
            q.expect("market: cover of mtest", timeout=10)

            # another tab: nothing loads; back on the Market, the covers go on
            keys("2")
            time.sleep(0.3)
            q.buf = b""
            time.sleep(2.5)
            assert b"market: cover" not in q.buf, q.buf.decode(errors="replace")
            keys("1")
            q.expect("market: cover of", timeout=10)
            # the first cover (red, 128x80: fitted in the first square of the grid) arrives
            for _ in range(50):
                img_ = q.screendump()
                if is_red(img_, 70, 144):
                    break
                time.sleep(0.2)
            assert is_red(img_, 70, 144), "the cover of Market Test"
            text = screen(["Installed"])            # Pong: on the card already
            shot("covers")

            # A on Market Test: a question, then the download with its progress
            screen(["Market Test", "Get", "Details"])
            keys("\r")
            screen(["Download Market Test?", "free, license MIT", "Download", "Cancel"])
            shot("ask")
            keys("\r")
            screen(["%"])                            # the badge with the percent
            shot("download")
            q.expect("market: games/mtest/mtest.bm -> /carts/MTEST.BM", timeout=20)
            q.expect("market: Market Test installed", timeout=5)
            screen(["Installed", "Play"])
            keys("\r")                                  # A plays it
            q.expect("market game runs", timeout=15)
            time.sleep(1.0)

            # X: the details of Pong (installed another way)
            keys("d")
            keys("x")
            screen(["Market > Pong", "Play", "Download again", "Author", "Version", "1.0", "License", "MIT"])
            shot("details")
            keys("q")

            # Broken: its file is not the catalog's
            keys("ddd")
            screen(["Broken"])
            keys("\r\r")
            q.expect("market: Broken: not the file of the catalog", timeout=20)
            screen(["Retry"])

            # Snake: leaving the tab interrupts the download
            keys("a")
            screen(["Snake", "Get"])
            keys("\r\r")
            q.expect("market: downloading games/snake/snake.bm", timeout=10)
            keys("2")
            q.expect("market: download of Snake interrupted", timeout=10)
            text = screen(["Market Test"])             # the Games tab has the new game
            assert "Snake" not in text, text
        finally:
            q.close()

        part = os.path.join(tmp, "part.img")
        with open(img, "rb") as f, open(part, "wb") as o:
            f.seek(2048 * 512)
            o.write(f.read())
        fsck = subprocess.run(["fsck.vfat", "-n", part], capture_output=True, text=True)
        assert fsck.returncode == 0, fsck.stdout + fsck.stderr
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
        carts = subprocess.run(["mdir", "-b", "-i", part, "::/CARTS"], capture_output=True, text=True,
                               env=env).stdout.upper()
        assert "MTEST.BM" in carts and "SNAKE" not in carts and "BROKEN" not in carts, carts
        cache = subprocess.run(["mdir", "-b", "-i", part, "::/BM/MARKET"], capture_output=True, text=True,
                               env=env).stdout.upper()
        assert "INDEX.TXT" in cache and "INDEX.SIG" in cache and "GAMES.TXT" in cache and ".PNG" in cache, cache
        owned = subprocess.run(["mtype", "-i", part, "::/BM/MARKET/GAMES.TXT"], capture_output=True, text=True,
                               env=env).stdout
        assert owned.startswith("mtest ") and owned.rstrip().endswith(" /carts/MTEST.BM"), owned
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def test_update(b, opts):
    """M19 step 4: Settings > System > Check for updates reads a release
    signed with a test key (here from the SD card: update_url=sd:/release/,
    bm/release.pem), says which files would change (a game the same, one
    changed, one not on the card, the certificates, both kernels); Install
    asks first, downloads and checks everything, keeps the old kernel in
    /bm/backup, writes, and the console restarts. Then the card holds the
    release's files, is a clean FAT32 volume, and the game deleted before is
    still not there."""
    tmp = tempfile.mkdtemp(prefix="bm-update-")
    try:
        key, pub = os.path.join(tmp, "key.pem"), os.path.join(tmp, "release.pem")
        subprocess.run(["openssl", "genpkey", "-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-256",
                        "-out", key], check=True, capture_output=True)
        subprocess.run(["openssl", "pkey", "-in", key, "-pubout", "-out", pub], check=True, capture_output=True)
        snake2 = os.path.join(tmp, "snake.bm")
        with open(snake2, "wb") as f:
            f.write(mkbm.pack(b"function _draw() cls(2) end", title="Snake", author="bm"))
        rel = os.path.join(tmp, "release")
        k6 = os.path.join(os.path.dirname(b("kernel7.img")), "kernel.img")    # b() gives kernel7 with --kernel7
        k7 = b("kernel7.img")
        if not ZERO2:                   # not built (make ZERO2=1): one with its mark
            k7 = os.path.join(tmp, "kernel7.img")
            with open(k7, "wb") as f:
                f.write(b"\0\0\0\0bmK7" + bytes(range(256)) * 32)
        own = "/kernel7.img" if KERNEL7 else "/kernel.img"
        other = "/kernel.img" if KERNEL7 else "/kernel7.img"
        files = [(k6, "/kernel.img"), (k7, "/kernel7.img"),
                 (snake2, "/carts/snake.bm"), (b("carts/pong.bm"), "/carts/pong.bm"),
                 (b("carts/shooter.bm"), "/carts/shooter.bm"),
                 (os.path.join(HERE, "..", "boot", "ca.pem"), "/bm/ca.pem")]
        args = [sys.executable, os.path.join(HERE, "..", "scripts", "mkrelease.py"), rel, "--version", "v9.9.9",
                "--commit", "abc1234", "--key", key, "--pub", pub]
        for src, path in files:
            args += ["--file", f"{src}:{path}"]
        r = subprocess.run(args, capture_output=True, text=True)
        assert r.returncode == 0, r.stdout + r.stderr
        old_kernel = os.path.join(tmp, "old.img")
        with open(old_kernel, "wb") as f:
            f.write(b"\0\0\0\0bmK6" + bytes(4088))             # an old kernel, to be kept
        old_ca = os.path.join(tmp, "old.pem")
        with open(old_ca, "w") as f:
            f.write("# the certificates of before\n")
        cfg = os.path.join(tmp, "config.txt")
        with open(cfg, "w") as f:
            f.write("layout=us\nwifi_boot=0\nupdate_url=sd:/release/\n")
        img = os.path.join(tmp, "sd.img")
        mksd.build(img, [(cfg, "bm/config.txt"), (pub, "bm/release.pem"), (old_ca, "bm/ca.pem"),
                         (old_kernel, "kernel.img"), (b("carts/pong.bm"), "carts/pong.bm"),
                         (b("carts/snake.bm"), "carts/snake.bm")]
                   + [(os.path.join(rel, n), "release/" + n) for n in os.listdir(rel)])
        q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])

        def keys(k):
            for c in k:
                q.send(c)
                time.sleep(0.25)

        def screen(want, tries=20):
            for _ in range(tries):
                _, text = settled_screen(q, lambda i, t: True, tries=1)
                joined = "\n".join(text)
                if all(w in joined for w in want):
                    return joined
                time.sleep(0.2)
            raise AssertionError(f"want {want} on the screen:\n{joined}")

        try:
            q.expect(MENU, timeout=30)
            time.sleep(0.5)
            keys("5")                               # Settings, then Updates
            screen(["Settings", "Controllers", "Updates"])
            keys("sss")
            keys("\r")
            screen(["Settings > Updates", "Version"])
            keys("s")
            screen(["Check for updates", "not checked", "The latest release on GitHub"])
            keys("\r")
            out = q.expect("back to the menu", timeout=30).decode(errors="replace")
            for w in ("latest release: \x1b[1mv9.9.9", "commit abc1234, signed: good",
                      "v9.9.9 can be installed"):
                assert w in out, out
            # a build of the sources, or later than a tag (git describe): either may update
            assert "a build of the sources" in out or "newer than this kernel" in out, out
            lines = {l.split()[0]: l for l in out.replace("\r", "").splitlines() if l.startswith("  /")}
            assert lines["/kernel.img"].endswith("changed") and lines["/kernel7.img"].endswith("new"), lines
            assert lines["/carts/snake.bm"].endswith("changed") and lines["/carts/pong.bm"].endswith("same"), lines
            assert lines["/carts/shooter.bm"].endswith("not on the card (Market)"), lines
            assert lines["/bm/ca.pem"].endswith("changed"), lines
            keys("\r")                              # A: back to the panel
            screen(["Check for updates", "v9.9.9: 4 files"])
            keys("s")                               # the row under it: install
            screen(["Install the update", "v9.9.9", "Keeps the old kernels in /bm/backup"])
            keys("\r")
            screen(["Install bm v9.9.9?", "The console restarts when it is done.", "Install"])
            if opts.shots:
                _save_png(q.screendump(), os.path.join(opts.shots, "update-ask.png"))
            keys("\r")
            out = q.expect("Restarting in 1", timeout=90).decode(errors="replace")
            assert "v9.9.9 installed" in out and "Restarting in 3" in out, out   # counted down
            for w in ("all 4 files downloaded and checked", "/kernel.img kept in /bm/backup",
                      "written /carts/snake.bm", "written /bm/ca.pem", "written /kernel7.img",
                      "written /kernel.img"):
                assert w in out, out
            assert out.index("written " + other) < out.index("written " + own), "this board's kernel last"
            q.expect(MENU, timeout=40)              # restarted
        finally:
            q.close()

        part = os.path.join(tmp, "part.img")
        with open(img, "rb") as f, open(part, "wb") as o:
            f.seek(2048 * 512)
            o.write(f.read())
        fsck = subprocess.run(["fsck.vfat", "-n", part], capture_output=True, text=True)
        assert fsck.returncode == 0, fsck.stdout + fsck.stderr
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")

        def read(path):
            r = subprocess.run(["mtype", "-i", part, "::" + path], capture_output=True, env=env)
            return r.stdout if r.returncode == 0 else None
        assert read("/KERNEL.IMG") == open(k6, "rb").read(), "kernel.img installed"
        assert read("/KERNEL7.IMG") == open(k7, "rb").read(), "kernel7.img installed"
        assert read("/BM/BACKUP/KERNEL.IMG") == open(old_kernel, "rb").read(), "the old kernel kept"
        assert read("/CARTS/SNAKE.BM") == open(snake2, "rb").read(), "the changed game"
        assert read("/CARTS/PONG.BM") == open(b("carts/pong.bm"), "rb").read(), "the same game untouched"
        assert read("/CARTS/SHOOTER.BM") is None, "a game not on the card stays off it"
        assert read("/BM/CA.PEM") == open(os.path.join(HERE, "..", "boot", "ca.pem"), "rb").read(), "ca.pem"
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def test_publish(b, opts):
    """M25 step 6: X on a game of the SD card, "Publish to the Market": the
    folder from the file name, today's version, the license chosen with
    left/right, the token from bm/config.txt; sending asks first, then runs
    on the text console (here without a network: it says so) and A goes
    back to the menu. Then "Send to a nearby console" (M24): without a
    network, nobody to send to."""
    tmp = tempfile.mkdtemp(prefix="bm-publish-")
    try:
        img = os.path.join(tmp, "sd.img")
        cfg = os.path.join(tmp, "config.txt")
        with open(cfg, "w") as f:
            f.write("layout=us\nwifi_boot=0\ngithub_token=github_pat_test\n")
        cart = os.path.join(tmp, "game.bm")
        with open(cart, "wb") as f:
            f.write(mkbm.pack(MARKET_GAME.encode(), title="My Game", author="tests"))
        mksd.build(img, [(cfg, "bm/config.txt"), (cart, "carts/My Game.bm")])
        q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])

        def keys(k):
            for c in k:
                q.send(c)
                time.sleep(0.25)

        def screen(want, gone=()):
            for _ in range(20):
                _, text = settled_screen(q, lambda i, t: True, tries=1)
                joined = "\n".join(text)
                if all(w in joined for w in want) and not any(g in joined for g in gone):
                    return joined
                time.sleep(0.2)
            raise AssertionError(f"want {want}, not {gone}, on the screen:\n{joined}")

        try:
            q.expect(MENU, timeout=30)
            time.sleep(0.5)
            keys("x")                               # the options of My Game
            screen(["My Game", "Play"])
            keys("ssssssss")                        # after the seven "Open in"
            screen(["Publish to the Market", "A pull request with your GitHub token"])
            keys("\r")
            screen(["Publish > My Game", "games/my-game", "License", "MIT", "GitHub token", "set",
                    "f-accomando/bm-market", "Send the pull request"])
            if opts.shots:
                _save_png(q.screendump(), os.path.join(opts.shots, "publish.png"))
            keys("ss")                              # the license: right, right, left
            keys("d")
            screen(["< CC-BY-4.0 >"])
            keys("d")
            screen(["< CC-BY-SA-4.0 >"])
            keys("a")
            screen(["< CC-BY-4.0 >"])
            keys("sss")                             # Send: a question first
            keys("\r")
            screen(["Publish My Game?", "Pull request to the Market, license CC-BY-4.0", "Publish"])
            keys("\r")
            q.expect("bm Market: publishing My Game", timeout=10)
            q.expect("games/my-game, version 1, license CC-BY-4.0, to f-accomando/bm-market", timeout=5)
            q.expect("no network: connect in Settings > WiFi and network", timeout=5)
            q.expect("back to the menu", timeout=5)
            keys("\r")
            screen(["Publish > My Game"])           # back on the panel

            # M24: send to a nearby console (here no network: nobody)
            keys("q")
            screen(["My Game", "Publish to the Market"], gone=["Publish > My Game"])
            keys("s")
            screen(["Send to a nearby console", "To a console on this network with the Market tab open"])
            keys("\r")
            screen(["Send > My Game", "This console", "no network", "No console nearby yet"])
            keys("s")
            screen(["Connect in Settings > WiFi and network"])
        finally:
            q.close()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def test_lib_tab(b, opts):
    """The Lib tab (docs/RISORSE.md): after Dev; left/right the groups, the
    list of the files with their resources (the resource files of /bm/lib,
    then the games), the details of the selected one (author, licence and
    tags from INFO once the selection rests); A on a game's model opens it
    in bm Studio, and back in the menu the tab is there again."""
    import bmres
    tmp = tempfile.mkdtemp(prefix="bm-lib-")
    img = os.path.join(tmp, "sd.img")
    village, demo, sound = (bmres.read(b(p)) for p in ("carts/village.bm", "demo.bm", "sound.bm"))
    files = []

    def res(name, f):
        path = os.path.join(tmp, name)
        bmres.write(path, f)
        files.append((path, "bm/lib/" + name))
    house = bmres.extract_models(village, ["house"])
    info = bmres.info_of(house)
    bmres.kv_set(info["file"], "license", "CC0-1.0")
    bmres.kv_set(info["file"], "tags", "building, village")
    bmres.info_store(house, info)
    res("HOUSE.BMM", house)
    res("FLAG.BMI", bmres.extract_image(demo, rect=(8, 0, 16, 8), name="flag", frames=2, fps=6))
    res("JUMP.BMS", bmres.extract_sounds(sound, sfx=["JUMP"]))
    res("DEMO.BMT", bmres.extract_map(demo))
    res("VILLAGE.BMC", bmres.extract_palette(village))
    kit = bmres.extract_kit(village)
    kit.name = "Village kit"
    res("VILLAGE.BMK", kit)
    mksd.build(img, files + [(b("carts/village.bm"), "carts/village.bm"), (b("demo.bm"), "carts/game.bm")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])

    def keys(*ks, gap=0.3):
        for k in ks:
            q.send(k)
            time.sleep(gap)

    def screen(want, gone=(), tries=40):
        for _ in range(tries):
            _, text = settled_screen(q, lambda i, t: all(any(w in l for l in t) for w in want), tries=2)
            if all(any(w in l for l in text) for w in want) and not any(g in l for l in text for g in gone):
                return "\n".join(text)
            time.sleep(0.25)
        raise AssertionError(f"not on the screen: {want} (or still: {gone})\n" + "\n".join(text))

    def shot(name):
        if opts.shots:                          # a whole frame: the bar and the hints drawn
            img_, _ = settled_screen(q, lambda i, t: "Games" in t[1] and "Monitor" in t[21], tries=20)
            _save_png(img_, os.path.join(opts.shots, f"lib-{name}.png"))

    def box(img_):
        """the pixels of the preview box (x 280-615, y 92-219) that are not its background"""
        return [pixel(img_, x, y) for y in range(92, 220, 2) for x in range(280, 616, 2)
                if pixel(img_, x, y) != (0x10, 0x10, 0x16)]

    def drawn(least=200, tries=20):
        for _ in range(tries):
            px = box(q.screendump())
            if len(px) >= least:
                return px
            time.sleep(0.25)
        raise AssertionError(f"the preview is empty ({len(px)} pixels)")

    try:
        q.expect(MENU, timeout=30)
        time.sleep(0.5)
        keys("4")
        q.expect("lib: ", timeout=20)
        text = screen(["Models", "Images", "Sounds", "Maps", "Palettes", "Kits", "HOUSE.BMM", "house",
                       "VILLAGE.BMK", "123 vertices, 180 faces", "from bm/lib/HOUSE.BMM"])
        assert img_tabs(q, ["Lib"]) == ["Lib"], img_tabs(q)
        # the INFO lines and the preview once the selection rests: the house turns
        screen(["bm   CC0-1.0", "tags: building, village"])
        a = drawn()
        time.sleep(0.6)
        b_ = drawn()
        assert a != b_, "the model does not turn"
        shot("models")
        keys("d")
        screen(["FLAG.BMI", "flag", "8x8", "8x8 pixels, 2 frames", "sheet"], gone=["HOUSE.BMM"])
        drawn(2000)                                 # the flag, big, on the checkerboard
        shot("images")
        keys("d")
        screen(["JUMP.BMS", "JUMP", "sound effect", "from bm/lib/JUMP.BMS", "Play"])
        drawn(100)                                  # a bar for each note
        keys("v")                                   # Y: plays it
        q.expect("lib: playing sfx JUMP", timeout=10)
        shot("sounds")
        screen(["Play"], gone=["Stop"])             # short: it ends by itself
        keys("d")
        screen(["DEMO.BMT", "GAME.BM", "160x90", "map 160x90 tiles"])
        drawn(2000)
        shot("maps")
        keys("d")
        screen(["VILLAGE.BMC", "Studio Village", "120 col", "30 colours"])     # 121 with the clear one
        shot("palettes")
        cols = set(drawn(1000))
        assert len(cols) >= 10, f"the squares of the colours: {len(cols)}"     # 30 greens: 17 in RGB565
        keys("d")
        screen(["VILLAGE.BMK", "Village kit", "kit", "8 models, 1 animated", "sheet 256x256, 0 zones"])
        shot("kits")
        keys("d")                                   # round to Models
        screen(["HOUSE.BMM", "house"])
        # down to the villager of the game (not the kit's), then A: bm Studio on it
        for _ in range(30):
            rows = screen(["Models"]).splitlines()
            if rows[14][35:].strip() == "villager" and rows[16][35:].strip() == "from carts/village.bm":
                break
            keys("s", gap=0.2)
        text = screen(["from carts/village.bm", "112 vertices, 168 faces"])
        shot("villager")
        keys("\r", gap=1.0)
        screen(["build", "models", "TOOLS"])
        keys("\x1b", gap=0.6)
        screen(["bm Studio", "Exit bm Studio"])
        keys("\x1b[A", "\r", gap=0.6)
        q.expect("lib: ", timeout=20)                # read again: a tool may have saved
        screen(["Models", "VILLAGE.BM", "from carts/village.bm", "last: bm Studio on VILLAGE.BM"])
        keys("5")
        screen(["Controllers"])
        assert img_tabs(q, ["Settings"]) == ["Settings"], img_tabs(q)
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
        screen_img, runs = wait_bar_icons(
            q, lambda r, i: len(r) == 3 and all(blue_number(i, x) for x in r))
        if opts.shots:
            _save_png(screen_img, os.path.join(opts.shots, "home-pads.png"))
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
        time.sleep(1.0)                       # the bar: the keyboard icon (M27)
        shot_, runs = wait_bar_icons(q, lambda r, i: len(r) == 1 and len(prompt_spans(i, 21)) == 5)
        assert len(runs) == 1 and 20 <= runs[0][1] - runs[0][0] <= 27, runs
        assert not blue_number(shot_, runs[0]), "USB: a white number"
        # the hints: player 1's keyboard, Enter Play, C Options, Ctrl+Shift+Esc Monitor
        widths = [x1 - x0 for x0, x1 in prompt_spans(shot_, 21)]
        assert len(widths) == 5 and widths[0] >= 34 and widths[1] <= 16 and widths[2] >= 28 and \
            widths[3] >= 28 and 20 <= widths[4] <= 28, widths
        sendkeys(q, "e")                      # E is R1: the Dev tab
        img_, text = settled_screen(q, lambda i, t: tabs_lit(i) == ["Dev"])
        assert tabs_lit(img_) == ["Dev"], "\n".join(text)
        sendkeys(q, "q")                      # Q is L1: back to Games
        img_, text = settled_screen(q, lambda i, t: tabs_lit(i) == ["Games"])
        assert tabs_lit(img_) == ["Games"], "\n".join(text)
        sendkeys(q, "c")                      # C is the X button: the options (M27)
        # (with the tools the info rows, Author..., are below: the panel scrolls)
        opts_row = "Open in bm Studio"
        # both rows checked below: a frame caught half drawn may have one alone
        _, text = settled_screen(q, lambda i, t: any(opts_row in l for l in t) and any("Play" in l for l in t))
        assert any(opts_row in l for l in text) and any("Play" in l for l in text), "\n".join(text)
        sendkeys(q, "x")                      # X is the B button: back
        _, text = settled_screen(q, lambda i, t: not any(opts_row in l for l in t))
        assert not any(opts_row in l for l in text), "\n".join(text)
        sendkeys(q, "ret")
        q.expect("playing game.bm", timeout=10)
        time.sleep(1.5)
        sendkeys(q, "ctrl-esc")               # Ctrl+Esc leaves the game (as PS)
        q.expect("update+draw", timeout=15)
        time.sleep(0.5)
        sendkeys(q, "ctrl-shift-esc")         # the menu: Ctrl+Shift+Esc to the monitor
        q.expect("back to the monitor", timeout=10)
        q.expect("> ")
    finally:
        q.close()
        os.remove(img)


def test_menu_scroll(b, opts):
    """Three rows of covers and two on screen: the scroll bar at the right
    says where they are, so the row scrolled out at the top is not taken for
    gone (15 cartridges: three rows of six)."""
    tmp = tempfile.mkdtemp(prefix="bm-scroll-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [(b("demo.bm"), f"carts/g{i}.bm") for i in range(1, 16)])
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


def arrow_at(img, x, y):
    """The pointer's arrow (M32) with its tip at (x, y): on its sixth row a
    black outline pixel, then four white ones (both sizes of the arrow)."""
    def white(p): return min(p) > 230
    def black(p): return max(p) < 24
    return black(pixel(img, x, y + 5)) and all(white(pixel(img, x + i, y + 5)) for i in (1, 2, 3, 4))


MOUSE_CARTS = ["astrowing", "hunt", "pong", "snake", "shooter", "village", "kitchen", "titan"]
# their titles, in the order of the menu
MOUSE_TITLES = ["Astro Wing", "Chaos Kitchen", "Hunter's Night", "Pong", "Snake", "Star Shooter",
                "Studio Village", "Titan Clash"]


def cover_xy(i):
    """The middle of cover i of the grid (first two rows on screen): squares
    of 88 every 100 pixels, six a row (2026-10-04)."""
    return 26 + (i % 6) * 100 + 44, 100 + (i // 6) * 100 + 44


def test_usb_mouse(b, opts):
    """M32: a USB mouse next to the USB keyboard (both behind a hub; QEMU's
    usb-tablet, moved to absolute positions over QMP). The bar shows the
    keyboard and a white mouse without a number; the arrow is drawn where
    the pointer is; moving over a cover selects it, the wheel moves by rows,
    the keys hide the arrow; a click on a tab changes it, the right button
    opens a cover's options, a click outside the panel closes it, a click
    on a cover plays it."""
    tmp = tempfile.mkdtemp(prefix="bm-mouse-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [(b(f"carts/{n}.bm"), f"carts/{n}.bm") for n in MOUSE_CARTS])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}",
                               "-device", "usb-hub,port=1", "-device", "usb-kbd,port=1.2",
                               "-device", "usb-tablet,port=1.3"])
    try:
        out = q.expect("cartridge menu", timeout=90).decode(errors="replace")
        assert "usb: port 3: if0 class 03/00/00 ep 81 mps 8" in out and " tablet" in out, out
        assert "usb: tablet (mouse) 0627:0001 'QEMU USB Tablet' (hub port 3), wheel" in out, out
        assert "usb: keyboard 0627:0001 'QEMU USB Keyboard'" in out, out
        # its icon once it has moved (2026-10-04: a keyboard's dongle declares
        # a mouse with none behind it)
        time.sleep(1.0)
        assert len(bar_icons(q.screendump())) == 1, "a mouse icon before the mouse did anything"
        q.pointer(320, 180)
        shot_ = wait_icons(q, 2)
        runs = bar_icons(shot_)
        assert len(runs) == 2 and not blue_number(shot_, runs[0]) and not blue_number(shot_, runs[1]), runs
        assert runs[1][1] - runs[1][0] <= 13, runs          # the mouse: narrow, no number
        assert arrow_at(shot_, 320, 180), "no arrow in the middle"
        # over a cover: it is selected, its title shown
        x, y = cover_xy(2)
        q.pointer(x, y)
        shot_ = wait_screen(q, lambda s_: arrow_at(s_, x, y) and title_is(MOUSE_TITLES[2])(s_))
        assert arrow_at(shot_, x, y), "the arrow did not follow"
        assert MOUSE_TITLES[2] in screen_text(shot_)[4], screen_text(shot_)[4]
        # the wheel: a row down (the last cover of the shorter row)
        q.click("wheel-down")
        assert MOUSE_TITLES[7] in screen_text(wait_screen(q, title_is(MOUSE_TITLES[7])))[4]
        # the keys move the selection: the arrow goes away until it moves
        sendkeys(q, "left")
        shot_ = wait_screen(q, lambda s_: not arrow_at(s_, x, y) and title_is(MOUSE_TITLES[6])(s_))
        assert not arrow_at(shot_, x, y), "the arrow stayed with the keys"
        assert MOUSE_TITLES[6] in screen_text(shot_)[4], screen_text(shot_)[4]

        def click_at(px, py, button="left"):        # once the arrow is there
            q.pointer(px, py)
            assert arrow_at(wait_screen(q, lambda s_: arrow_at(s_, px, py)), px, py), (px, py)
            q.click(button)
        # a click on Dev changes the tab, on Games back (Market | Games | Dev;
        # the Market off the screen at the left)
        click_at(132, 24)
        assert "bm SDK" in screen_text(wait_screen(q, title_is("bm SDK")))[4]
        click_at(68, 24)
        wait_screen(q, title_is(MOUSE_TITLES[6]))
        # the right button on a cover: its options; a click outside closes them
        x, y = cover_xy(1)
        click_at(x, y, "right")
        text = "\n".join(screen_text(wait_screen(q, lambda s_: "Open in the SDK" in "".join(screen_text(s_)))))
        assert "Play" in text and "Open in the SDK" in text, text
        click_at(30, 200)
        text = "\n".join(screen_text(wait_screen(
            q, lambda s_: "Open in the SDK" not in "".join(screen_text(s_)))))
        assert "Open in the SDK" not in text and MOUSE_TITLES[1] in text, text
        # a click on a cover plays it
        click_at(*cover_xy(0))
        q.expect("playing astrowing.bm", timeout=10)
        q.expect("bm: loaded", timeout=10)
        time.sleep(1.0)
        q.send("q")
        q.expect("update+draw", timeout=15)
    finally:
        q.close()
        shutil.rmtree(tmp, ignore_errors=True)


MOUSE_CART = r"""
local last
function _init()
  log("before " .. tostring(mouse()))
  log("enabled " .. tostring(mouse(true)))
end
function _update()
  local x, y, b, w, shown = mouse()
  local s = x and string.format("%d,%d b%d w%d %s", x, y, b, w, tostring(shown)) or "nil"
  if mousep(0) then s = s .. " click" end
  if mousep(1) then s = s .. " right" end
  if s ~= last then last = s; log("mouse " .. s) end
end
function _draw() cls(1) end
"""


def test_mouse_cart(b, opts):
    """M32: a cartridge has the pointer only when it asks (mouse(true)):
    mouse() gives its position in the cartridge's pixels (320x180 here),
    the buttons, the wheel; mousep() the clicks; the small arrow is drawn
    over the frame. With mouse=off in bm/config.txt there is no pointer
    anywhere: no icon, mouse(true) is false, mouse() nil."""
    tmp = tempfile.mkdtemp(prefix="bm-mousecart-")
    cart = os.path.join(tmp, "mouse.bm")
    with open(cart, "wb") as f:
        f.write(mkbm.pack(MOUSE_CART.encode(), title="Mouse test", res=(320, 180)))
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [(cart, "carts/mouse.bm")])
    tablet = ["-drive", f"if=sd,format=raw,file={img}", "-device", "usb-tablet,port=1"]
    q = Qemu(b("kernel.img"), tablet)
    try:
        q.expect("cartridge menu", timeout=90)
        time.sleep(1.0)
        x, y = cover_xy(0)
        q.pointer(x, y)
        time.sleep(0.3)
        q.click()
        q.expect("playing mouse.bm", timeout=10)
        out = q.expect("enabled true", timeout=10).decode(errors="replace")
        assert "before nil" in out, out
        q.pointer(160, 45, 320, 180)
        q.expect("mouse 160,45 b0 w0 true", timeout=10)
        shot_ = wait_screen(q, lambda s_: s_[:2] == (320, 180) and arrow_at(s_, 160, 45))
        assert shot_[:2] == (320, 180) and arrow_at(shot_, 160, 45), shot_[:2]
        q.click()
        q.expect("mouse 160,45 b1 w0 true click", timeout=10)
        q.click("right")
        q.expect(" right", timeout=10)
        q.click("wheel-up")
        q.expect("w1 true", timeout=10)
        q.send("q")
        q.expect("update+draw", timeout=15)
    finally:
        q.close()
    # a square cartridge (256x256 in the middle of a 480x270 screen): the
    # arrow is in its box
    square = os.path.join(tmp, "square.bm")
    with open(square, "wb") as f:
        f.write(mkbm.pack(MOUSE_CART.encode(), title="Mouse square", res=(256, 256)))
    mksd.build(img, [(square, "carts/square.bm")])
    q = Qemu(b("kernel.img"), tablet)
    try:
        q.expect("cartridge menu", timeout=90)
        wait_icons(q, 1)
        q.send("\r")
        q.expect("playing square.bm", timeout=10)
        q.expect("enabled true", timeout=10)
        q.pointer(128, 64, 256, 256)
        q.expect("mouse 128,64 b0 w0 true", timeout=10)
        shot_ = wait_screen(q, lambda s_: arrow_at(s_, 112 + 128, 7 + 64))
        assert shot_[:2] == (480, 270) and arrow_at(shot_, 112 + 128, 7 + 64), shot_[:2]
        q.send("q")
        q.expect("update+draw", timeout=15)
    finally:
        q.close()
    # the whole console without the pointer
    with open(os.path.join(tmp, "config.txt"), "w") as f:
        f.write("mouse=off\n")
    mksd.build(img, [(cart, "carts/mouse.bm"), (os.path.join(tmp, "config.txt"), "bm/config.txt")])
    q = Qemu(b("kernel.img"), tablet)
    try:
        out = q.expect("cartridge menu", timeout=90).decode(errors="replace")
        assert ", mouse off" in out, out
        shot_ = wait_icons(q, 0)
        assert bar_icons(shot_) == [] and not arrow_at(shot_, 320, 180), bar_icons(shot_)
        q.pointer(*cover_xy(0))
        time.sleep(0.3)
        q.click()
        time.sleep(1.0)
        assert b"playing" not in q.buf, "the click played"
        q.send("\r")
        q.expect("playing mouse.bm", timeout=10)
        q.expect("enabled false", timeout=10)
        q.expect("bm: loaded", timeout=10)
        q.pointer(100, 100, 320, 180)
        time.sleep(0.5)
        q.send("q")
        out = q.expect("update+draw", timeout=15).decode(errors="replace")
        assert "mouse 100" not in out and "mouse nil" in out, out
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
        with open(b("texroom.bm"), "rb") as f:
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
            # page 0 can be caught half drawn (QEMU ignores the page flips):
            # the textures too, or another screendump
            if grass > 300 and roof > 20 and sky > 300 and len(set(cols)) > 100:
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


def test_yharnam(b, opts):
    """Yharnam: a 256x256 cartridge, shown in the middle of a 480x270
    screen with black around it; the town made while you walk, lit by
    levels (fades, glow). Title, the animations (the hunter's, a creature's,
    a boss's), start (no area's name on screen, the user's wish), a dark
    night with warm lamps and fires, no Lua error."""
    BX, BY = 112, 7                             # the 256x256 box in the 480x270 screen

    def box(img):
        w, h, px = img
        out = bytearray()
        for y in range(BY, BY + 256):
            out += px[(y * w + BX) * 3:(y * w + BX + 256) * 3]
        return 256, 256, bytes(out)

    q = Qemu(b("kernel.img"))
    try:
        q.expect(MENU, timeout=30)
        time.sleep(0.5)
        with open(b("carts/yharnam.bm"), "rb") as f:
            assert _upload(q, f.read())
        text = []
        for _ in range(20):
            time.sleep(0.25)
            img = q.screendump()
            if img[0] == 480:
                text = screen_text(box(img))
                if any("YHARNAM" in l for l in text):
                    break
        assert img[0] == 480 and img[1] == 270, img[:2]
        assert any("YHARNAM" in l for l in text) and any("A: START" in l for l in text), "\n".join(text)
        w, h, px = img
        for x, y in ((0, 0), (479, 269), (BX - 1, 128), (BX + 256, 128), (240, BY - 1), (240, BY + 256)):
            assert px[(y * w + x) * 3:(y * w + x) * 3 + 3] == b"\0\0\0", ("border", x, y)
        if opts.shots:
            _save_png(img, os.path.join(opts.shots, "yharnam-title.png"))
        q.send("c")                             # X: the animations of the hunter
        for _ in range(12):
            time.sleep(0.25)
            text = screen_text(box(q.screendump()))
            if any("idle  S" in l for l in text):
                break
        assert any("idle  S" in l for l in text), "\n".join(text)
        q.send("s")                             # the next animation
        time.sleep(0.5)
        text = screen_text(box(q.screendump()))
        assert any("walk  S" in l for l in text), "\n".join(text)
        if opts.shots:
            _save_png(q.screendump(), os.path.join(opts.shots, "yharnam-gallery.png"))
        q.send("v")                             # Y: the next one, the first creature
        time.sleep(0.5)
        text = screen_text(box(q.screendump()))
        assert any("Townsman" in l for l in text) and any("idle  S" in l for l in text), "\n".join(text)
        for _ in range(4):                      # ... and on to the first boss
            q.send("v")
            time.sleep(0.3)
        q.send("d")                             # turned south-west: south-east, mirrored
        time.sleep(0.6)
        text = screen_text(box(q.screendump()))
        assert any("The Butcher" in l for l in text), "\n".join(text)
        if opts.shots:
            _save_png(q.screendump(), os.path.join(opts.shots, "yharnam-boss.png"))
        q.send("x")                             # B: back to the title
        time.sleep(0.5)
        q.send(" ")                             # A: start
        time.sleep(1.5)
        for k in "ssddwwaauok\t":               # walk, L1 lock, R1 heavy, B dodge, Select heal
            q.send(k)
            time.sleep(0.15)
        for _ in range(10):
            img = box(q.screendump())
            text = screen_text(img)
            cols = [tuple(img[2][i:i + 3]) for i in range(0, len(img[2]), 3)]
            dark = sum(r + g + b < 120 for r, g, b in cols)
            warm = sum(r > 200 and g > 120 and b < 150 for r, g, b in cols)
            if not any("A: START" in l for l in text) and warm > 20:
                break
            time.sleep(0.4)
        if opts.shots:
            _save_png(q.screendump(), os.path.join(opts.shots, "yharnam-play.png"))
        print(f"     yharnam: {dark} dark pixels of 65536, {warm} warm (lamps, fires)")
        assert not any("A: START" in l for l in text), "\n".join(text)
        assert not any(w in l for l in text for w in ("Square", "lamps")), "\n".join(text)
        assert dark > 30000, dark               # a night, nearly dark
        assert warm > 20, warm                  # warm lamps and fires
        q.send("\r")                            # Start: the pause, then its controls
        time.sleep(0.5)
        text = screen_text(box(q.screendump()))
        assert any("PAUSE" in l for l in text) and any("Controls" in l for l in text), "\n".join(text)
        q.send("s")
        time.sleep(0.3)
        q.send(" ")
        time.sleep(0.6)
        text = screen_text(box(q.screendump()))
        assert any("CONTROLS" in l for l in text) and any("lock on" in l for l in text), "\n".join(text)
        if opts.shots:
            _save_png(q.screendump(), os.path.join(opts.shots, "yharnam-controls.png"))
        # the dev kit: the performance overlay ('p' from the serial line, F11 on a
        # keyboard): simple, detailed (the frame's phases), off
        q.send("p")
        time.sleep(0.6)
        text = screen_text(box(q.screendump()))
        assert any("fps" in l and "ms" in l for l in text), "\n".join(text)
        assert not any("update" in l for l in text), "\n".join(text)
        if opts.shots:
            _save_png(q.screendump(), os.path.join(opts.shots, "yharnam-perf.png"))
        q.send("p")
        time.sleep(0.6)
        text = screen_text(box(q.screendump()))
        assert any("update" in l and "ms" in l for l in text) and any("draw" in l for l in text), "\n".join(text)
        if opts.shots:
            _save_png(q.screendump(), os.path.join(opts.shots, "yharnam-perf-detailed.png"))
        q.send("p")
        time.sleep(0.6)
        text = screen_text(box(q.screendump()))
        assert not any("fps" in l for l in text), "\n".join(text)
        q.send("q")
        out = q.expect("update+draw", timeout=10).decode(errors="replace")
        assert "stopped with an error" not in out, out
        assert '"Yharnam"' in out, out[-300:]
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


def test_studio_animator(b, opts):
    """bm Studio and bm Animator on the console (a game's options, "Open in
    bm Studio"; the menu's "Open in bm Animator"): bm Studio shows the
    village's models with its tools (select, vertex, paint, tiles, models);
    a new project gets a block, is saved, tried (its viewer plays) and comes
    back; bm Animator opens it (cart_tool) and gives it a skeleton and an
    animation; then the village's villager plays, its bones, its sprites go
    into the sheet. The file holds what the kernel reads."""
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
        # a whole frame: the tab bar and the status bar are drawn last
        if opts.shots:
            img_, _ = settled_screen(q, lambda i, t: "menu" in t[0] and t[21].strip() != "", tries=20)
            _save_png(img_, os.path.join(opts.shots, f"{name}.png"))

    F1, F2, F3, F4 = "\x1bOP", "\x1bOQ", "\x1bOR", "\x1bOS"
    UP, DOWN, RIGHT, ESC, TAB = "\x1b[A", "\x1b[B", "\x1b[C", "\x1b", "\t"
    try:
        q.expect(MENU, timeout=30)
        time.sleep(0.5)
        screen(["Games", "Studio Village"])
        keys("x")
        screen(["Open in the SDK", "Open in bm Code", "Open in the Sound editor", "Open in bm Studio"])
        keys("s", "s", "s", "s", "\r")         # Play, SDK, Code, Sound, Studio
        screen(["build", "models", "TOOLS", "MODEL", "model 1/8: ground"])
        shot("studio-build")
        keys("3", "a")
        screen(["SELECT", "faces chosen"])
        shot("studio-select")
        keys("4")
        screen(["VERTEX", "faces)"])
        shot("studio-vertex")
        keys("5", "\r", gap=0.5)
        screen(["PAINT", " at "])
        shot("studio-paint")
        keys(ESC, "1", TAB, "d")
        screen(["tiles: sheet 256x256", "2 x 1 tiles"])
        shot("studio-tiles")
        keys(TAB, F2)
        screen(["MODELS 8", "villager"])
        shot("studio-models")

        # a new project: a block, saved as CUBE.BM
        keys(ESC, gap=0.6)
        screen(["bm Studio", "New project", "Exit bm Studio"])
        keys(DOWN, DOWN, "\r", gap=0.4)         # Continue, Open..., New project
        screen(["BLOCK", "cell 0,0,0"])
        keys(" ")
        screen(["6 faces", "12 tri", "8 vert"])
        shot("studio-new")
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

        # try it: the viewer of a new project plays it, then bm Studio comes back
        keys("\x1b[15~", gap=1)                 # F5
        time.sleep(3)
        keys("q")                               # the game ends (its keys are a gamepad's)
        out = q.expect('bm: "New 3D project"', timeout=20).decode(errors="replace")
        assert "stopped with an error" not in out, out
        screen(["back from the game", "BLOCK"])  # the page it was on

        # bm Animator on the same file (cart_tool)
        keys(ESC, gap=0.6)
        screen(["Open in bm Animator"])
        for _ in range(8):
            keys(DOWN)                          # Continue ... Author, Open in bm Animator
        keys("\r", gap=1.0)
        screen(["play", "sprites", "MODELS", "no skeleton yet"])
        # a skeleton (root and a child) and an animation with a turn at 0.25 s:
        # the kernel checks each new ANIM section (cart_data) before drawing it
        keys(F2, "n", "n", gap=0.5)
        screen(["BONES 2", "root", "bone2", "tail of bone2"])
        keys(F3, "n", gap=0.5)
        screen(["anim1  1/1  smooth  loop  1.00 s"])
        keys(RIGHT, RIGHT, RIGHT, "w", gap=0.4)
        screen(["TURN root   0.25 s  frame 3  key"])
        shot("animator-keyframe")
        keys("\x13", gap=0.6)                   # Ctrl+S
        screen(["saved /carts/CUBE.BM"])

        # the village: the villager, its bones, its animations, its sprites
        keys(ESC, gap=0.6)
        keys(DOWN, "\r")                        # Open...
        screen(["/carts/CUBE.BM", "/carts/village.bm"])
        shot("animator-open")
        keys(DOWN, "\r", gap=1.0)               # CUBE.BM, then village.bm
        screen(["opened /carts/village.bm", "MODELS"])
        for _ in range(7):
            keys(DOWN)
        screen(["ANIMATIONS", "idle", "walk", "wave", "112 vertices, 168 triangles, 7 bones"])
        keys("k")
        shot("animator-player")
        keys(F2)
        screen(["BONES 7", "hips", "spine", "arm.L", "leg.R", "tail of hips"])
        shot("animator-rig")
        keys("v")
        screen(["SKIN", "faces chosen"])
        shot("animator-skin")
        keys("v", F3, "o")
        screen(["ANIMATIONS", "BONES", "idle  1/3  smooth  loop  2.00 s  onion"])
        shot("animator-animate")
        keys(F4)
        screen(["SPRITES", "villager", "idle", "384x192 pixels in the sheet"])
        shot("animator-sprites")
        keys("\r")
        screen(["32 sprites put in the sheet", "sspr("], tries=80)
        shot("animator-sprites-put")

        # out (not saved: asked twice)
        keys(ESC, gap=0.6)
        keys(UP, "\r", gap=0.6)                 # up from Continue: Exit bm Animator
        screen(["unsaved changes"])
        keys("\r")
        screen(["Games", "last: bm Animator on village"])
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
        assert 5 in secs or 2 in secs, sorted(secs)     # the sheet of the new project (starter tiles)
        village = subprocess.run(["mtype", "-i", part, "::/CARTS/VILLAGE.BM"], capture_output=True, env=env).stdout
        with open(b("carts/village.bm"), "rb") as f:
            assert village == f.read(), "the village was not saved: it stays as it was"
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def test_studio_assistant(b, opts):
    """M30 in bm Studio and bm Animator: F6 opens the assistant in its 3D
    mode; a request ("casa rossa") shows the recipe turning in the panel
    and Enter makes it a model; a rigged one ("mech") brings its skeleton
    and animations, which bm Animator plays (cart_tool on the saved file)
    and the kernel's ANIM section holds; the Animator's own F6 adds a
    dragon. The file is read back and checked."""
    tmp = tempfile.mkdtemp(prefix="bm-s3dai-")
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
            img_, _ = settled_screen(q, lambda i, t: "menu" in t[0] and t[21].strip() != "", tries=20)
            _save_png(img_, os.path.join(opts.shots, f"{name}.png"))

    F2, F6 = "\x1bOQ", "\x1b[17~"
    DOWN, ESC = "\x1b[B", "\x1b"
    try:
        q.expect(MENU, timeout=30)
        time.sleep(0.5)
        screen(["Games", "Studio Village"])
        keys("x")
        screen(["Open in bm Studio"])
        keys("s", "s", "s", "s", "\r")
        screen(["build", "models", "TOOLS", "model 1/8: ground"])
        # a new project, then the assistant: a house from words
        keys(ESC, gap=0.6)
        keys(DOWN, DOWN, "\r", gap=0.4)         # New project
        screen(["BLOCK", "cell 0,0,0"])
        keys(F6, gap=0.6)
        screen(["Assistant", "mesh", "type a question"])
        for ch in "casa rossa":
            keys(ch, gap=0.12)
        screen(["Casa (casetta col tetto)", "faces", "3D"])
        shot("studio-assistant")
        keys("\r", gap=1.0)
        screen(["the assistant's house: 35 faces, model house", "TOOLS"])
        shot("studio-assistant-house")
        # a rigged one: the mech, a new model with its skeleton
        keys(F6, gap=0.6)
        for ch in "mech":
            keys(ch, gap=0.12)
        screen(["Mech (robot da combattimento", "9 bones: idle walk fire"])
        keys("\r", gap=1.5)
        screen(["9 bones, 3 animations, model mech"])
        keys(F2)
        screen(["MODELS 2", "house", "mech"])
        shot("studio-assistant-models")
        # saved as AI.BM, then bm Animator on it: the mech's animations play
        keys(ESC, gap=0.6)
        for _ in range(4):
            keys(DOWN)
        keys("\r")
        screen(["file name"])
        for _ in range(8):
            keys("\x7f", gap=0.1)
        for ch in "AI\r":
            keys(ch, gap=0.1)
        screen(["saved /carts/AI.BM"])
        keys(ESC, gap=0.6)                      # the menu goes back to the page...
        keys(ESC, gap=0.6)                      # ...and opens again on Continue
        screen(["Open in bm Animator"])
        for _ in range(8):
            keys(DOWN)
        keys("\r", gap=1.5)
        screen(["play", "sprites", "MODELS", "house", "mech"], tries=80)
        keys(DOWN)
        screen(["ANIMATIONS", "idle", "walk", "fire", "9 bones"])
        shot("animator-assistant-mech")
        # the Animator's own F6: a dragon, played at once
        keys(F6, gap=0.6)
        for ch in "drago":
            keys(ch, gap=0.12)
        screen(["Drago", "10 bones: idle fly walk"])
        keys("\r", gap=1.5)
        screen(["the assistant's dragon:", "10 bones, 3 animations, model dragon"])
        screen(["ANIMATIONS", "fly"])
        shot("animator-assistant-dragon")
        keys("\x13", gap=0.8)                   # Ctrl+S
        screen(["saved /carts/AI.BM"])
        keys(ESC, gap=0.6)
        keys("\x1b[A", "\r", gap=0.6)           # Exit bm Animator
        screen(["Games"])
    finally:
        q.close()
    try:
        part = os.path.join(tmp, "part.img")
        with open(img, "rb") as f, open(part, "wb") as o:
            f.seek(2048 * 512)
            o.write(f.read())
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
        saved = subprocess.run(["mtype", "-i", part, "::/CARTS/AI.BM"], capture_output=True, env=env).stdout
        secs = dict(bmmesh.cart_sections(saved))
        models, _ = bmmesh.decode(secs[bmmesh.SEC_MESH])
        names = [m["name"] for m in models]
        assert names == ["house", "mech", "dragon"], names
        assert len(models[0]["faces"]) >= 60 and len(models[1]["faces"]) >= 1000, [len(m["faces"]) for m in models]
        anim = secs[bmmesh.SEC_ANIM]
        assert struct.unpack_from("<H", anim)[0] == 2, "two rigs: the mech's and the dragon's"
        nb, nc, nv = struct.unpack_from("<HHH", anim, 8 + 16)
        assert anim[8:12] == b"mech" and (nb, nc) == (9, 3), (anim[8:24], nb, nc)
        clip = 8 + 24 + nb * 44 + ((nv + 3) & ~3)
        assert anim[clip:clip + 4] == b"idle", anim[clip:clip + 16]
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def test_img2mesh(b, opts):
    """tools/img2mesh.py offline (the recorded replies: the mech in the part
    language) writes a .bm the console plays: bm Studio lists its models,
    bm Animator plays the mech's animations from its ANIM section."""
    tmp = tempfile.mkdtemp(prefix="bm-i2m-")
    cart = os.path.join(tmp, "img2mesh.bm")
    knight = os.path.join(tmp, "knight.ppm")
    subprocess.run([b("host/meshview"), "one", "knight", knight], check=True, capture_output=True)
    for name, rounds in (("mech", "1"), ("robot", "0")):
        subprocess.run([sys.executable, os.path.join(HERE, "..", "tools", "img2mesh.py"), knight, "-o", cart,
                        "--name", name, "--rounds", rounds, "--replay", os.path.join(HERE, "ai", "img2mesh", "replay"),
                        "--work", os.path.join(tmp, name), "--meshview", b("host/meshview")],
                       check=True, capture_output=True)
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [(cart, "carts/img2mesh.bm")])
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

    F2 = "\x1bOQ"
    DOWN, ESC = "\x1b[B", "\x1b"
    try:
        q.expect(MENU, timeout=30)
        time.sleep(0.5)
        screen(["Games", "mech"])
        keys("x")
        screen(["Open in bm Studio"])
        keys("s", "s", "s", "s", "\r", gap=0.4)
        screen(["build", "models", "TOOLS"])
        keys(F2)
        screen(["MODELS 2", "mech", "robot", "662 faces 1160 tri 702 vertices", "9 bones, 3 animations"])
        keys(ESC, gap=0.6)
        screen(["Open in bm Animator"])
        for _ in range(8):
            keys(DOWN)
        keys("\r", gap=1.5)
        screen(["play", "ANIMATIONS", "idle", "walk", "fire", "702 vertices, 1160 triangles, 9 bones"], tries=80)
        if opts.shots:
            img_, _ = settled_screen(q, lambda i, t: "menu" in t[0] and t[21].strip() != "", tries=20)
            _save_png(img_, os.path.join(opts.shots, "img2mesh-animator.png"))
    finally:
        q.close()
        shutil.rmtree(tmp, ignore_errors=True)


def test_meshy2mesh(b, opts):
    """tools/meshy2mesh.py offline (tests/ai/check_meshy.py makes a .glb and
    converts it): the cartridge with the textured box on its own sheet and
    the flat pyramid plays in bm Studio, which lists both models."""
    tmp = tempfile.mkdtemp(prefix="bm-meshy-")
    subprocess.run([sys.executable, os.path.join(HERE, "ai", "check_meshy.py"), tmp], check=True, capture_output=True)
    cart = os.path.join(tmp, "meshy", "meshy.bm")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [(cart, "carts/meshy.bm")])
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

    try:
        q.expect(MENU, timeout=30)
        time.sleep(0.5)
        screen(["Games", "thing"])
        keys("x")
        screen(["Open in bm Studio"])
        keys("s", "s", "s", "s", "\r", gap=0.4)
        screen(["build", "models", "TOOLS", "model 1/2: thing", "18 tri"])
        keys("\x1bOQ")                         # F2
        screen(["MODELS 2", "thing", "flat1"])
        if opts.shots:
            img_, _ = settled_screen(q, lambda i, t: "menu" in t[0] and t[21].strip() != "", tries=20)
            _save_png(img_, os.path.join(opts.shots, "meshy-studio.png"))
        keys("\x1b", gap=0.6)
        keys("\x1b[A", "\r", gap=0.8)           # Exit bm Studio (nothing changed)
        screen(["Games", "thing"])
        keys("\r", gap=0.5)                    # play: the viewer shows the first model
        q.expect("playing meshy.bm", timeout=20)
        time.sleep(3.0)
        img_, _ = settled_screen(q, lambda i, t: True, tries=1)
        # the box drawn: its red and blue texture halves on the screen
        w, h, px = img_
        reds = blues = 0
        for i in range(0, w * h * 3, 3 * 7):
            r, g, bl = px[i], px[i + 1], px[i + 2]
            reds += r > 120 and g < 80 and bl < 80
            blues += bl > 120 and r < 80 and g < 80
        assert reds > 200 and blues > 200, (reds, blues)
        if opts.shots:
            _save_png(img_, os.path.join(opts.shots, "meshy-viewer.png"))
        keys("q", gap=1.0)
        out = q.expect('bm: "thing"', timeout=20).decode(errors="replace")
        assert "stopped with an error" not in out, out
    finally:
        q.close()
        shutil.rmtree(tmp, ignore_errors=True)


def test_mesh_reduce(b, opts):
    """The polygon reducer on the console (src/bm/decimate.c, mesh_reduce):
    bm Studio's models page, "-" asks the triangles; the ground of the
    village (288 triangles) becomes 40, the counts say so, Ctrl+S writes
    the file and it holds the reduced model; the other models stay."""
    tmp = tempfile.mkdtemp(prefix="bm-reduce-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [(b("carts/village.bm"), "carts/village.bm")])
    with open(b("carts/village.bm"), "rb") as f:
        models0, _ = bmmesh.decode(dict(bmmesh.cart_sections(f.read()))[bmmesh.SEC_MESH])
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
            img_, _ = settled_screen(q, lambda i, t: "menu" in t[0] and t[21].strip() != "", tries=20)
            _save_png(img_, os.path.join(opts.shots, f"{name}.png"))

    F2, ESC, SAVE = "\x1bOQ", "\x1b", "\x13"
    try:
        q.expect(MENU, timeout=30)
        time.sleep(0.5)
        screen(["Games", "Studio Village"])
        keys("x")
        screen(["Open in bm Studio"])
        keys("s", "s", "s", "s", "\r")
        screen(["build", "models", "TOOLS", "model 1/8: ground"])
        keys(F2)
        screen(["MODELS 8", "ground", "288 tri"])
        shot("reduce-before")
        keys("-")
        screen(["triangles (now 288"])
        for _ in range(6):
            keys("\x7f", gap=0.1)
        for ch in "40\r":
            keys(ch, gap=0.1)
        # a collapse takes two triangles away: 40 or 39
        text = screen(["reduced to", "faces"])
        got = re.search(r"reduced to (\d+) triangles", text)
        assert got and 38 <= int(got.group(1)) <= 40, text
        shot("reduce-after")
        keys(SAVE, gap=0.8)
        screen(["saved /carts/village.bm"])
        keys(ESC, gap=0.6)
        screen(["bm Studio", "Exit bm Studio"])
        keys("\x1b[A", "\r", gap=0.6)
        screen(["Games"])
    finally:
        q.close()
    part = os.path.join(tmp, "part.img")
    with open(img, "rb") as f, open(part, "wb") as o:
        f.seek(2048 * 512)
        o.write(f.read())
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    saved = subprocess.run(["mtype", "-i", part, "::/CARTS/VILLAGE.BM"], capture_output=True, env=env).stdout
    models, _ = bmmesh.decode(dict(bmmesh.cart_sections(saved))[bmmesh.SEC_MESH])
    assert [m["name"] for m in models] == [m["name"] for m in models0], [m["name"] for m in models]
    assert 38 <= len(models[0]["faces"]) <= 40, len(models[0]["faces"])
    assert all(f[3] == bmmesh.TEXTURED for f in models[0]["faces"]), "the ground keeps its texture"
    for m, m0 in zip(models[1:], models0[1:]):
        assert len(m["faces"]) == len(m0["faces"]), (m["name"], len(m["faces"]), len(m0["faces"]))
    shutil.rmtree(tmp, ignore_errors=True)
    print(f"mesh_reduce: the ground {len(models0[0]['faces'])} -> {len(models[0]['faces'])} triangles, saved")


def test_picture_model(b, opts):
    """A model from a picture on the console: bm Studio's models page, "m"
    asks how (the outline cut out or turned, made here; or the image-to-3D
    service) and lists the pictures of /pics. The cutout is made on the
    ARM kernel: a red disc on white becomes the model hero with its
    texture (the village's sheet is in use: flat colours). The service's
    start fails at once with a clear message (QEMU has no WiFi) and
    nothing changes."""
    tmp = tempfile.mkdtemp(prefix="bm-pic-")
    img = os.path.join(tmp, "sd.img")
    pic = os.path.join(tmp, "hero.png")
    w = h = 40
    px = bytearray()
    for y in range(h):
        for x in range(w):
            px += bytes((220, 40, 40)) if (x - 20) ** 2 + (y - 20) ** 2 < 14 ** 2 else bytes((255, 255, 255))
    raw = b"".join(b"\0" + bytes(px[y * w * 3:(y + 1) * w * 3]) for y in range(h))

    def chunk(t, body):
        return struct.pack(">I", len(body)) + t + body + struct.pack(">I", zlib.crc32(t + body) & 0xFFFFFFFF)
    with open(pic, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
    cfg = os.path.join(tmp, "config.txt")
    with open(cfg, "w") as f:
        f.write("# bm settings (key=value)\nmeshy_key=msy_test_key_for_qemu\n")
    ca = os.path.join(HERE, "..", "boot", "ca.pem")
    mksd.build(img, [(b("carts/village.bm"), "carts/village.bm"), (pic, "pics/hero.png"), (cfg, "bm/config.txt"),
                     (ca, "bm/ca.pem")])
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
            img_, _ = settled_screen(q, lambda i, t: "menu" in t[0] and t[21].strip() != "", tries=20)
            _save_png(img_, os.path.join(opts.shots, f"{name}.png"))

    F2, ESC, DOWN, SAVE = "\x1bOQ", "\x1b", "\x1b[B", "\x13"
    try:
        q.expect(MENU, timeout=30)
        time.sleep(0.5)
        screen(["Games", "Studio Village"])
        keys("x")
        screen(["Open in bm Studio"])
        keys("s", "s", "s", "s", "\r")
        screen(["build", "models", "TOOLS", "model 1/8: ground"])
        keys(F2)
        screen(["MODELS 8", "picture"])
        # the outline, made here: the model hero
        keys("m")
        screen(["cutout: the picture's outline", "lathe:", "meshy.ai:"])
        shot("picture-ways")
        keys("\r")
        screen(["/pics/hero.png"])
        keys("\r", gap=1.5)
        text = screen(["cutout: the model hero", "MODELS 9", "hero"], tries=80)
        assert "flat colours (the sheet is in use)" in text, text
        shot("picture-cutout")
        keys(SAVE, gap=0.8)
        screen(["saved /carts/village.bm"])
        # the service: the start fails with the reason (no network, no clock
        # for TLS...), named after the service; nothing changes
        keys("m")
        screen(["meshy.ai:"])
        keys(DOWN, DOWN, "\r")
        screen(["/pics/hero.png"])
        keys("\r", gap=1.0)
        screen(["cannot start: meshy:"], tries=80)
        screen(["MODELS 9"])
        keys(ESC, gap=0.6)
        screen(["Model from picture...", "Exit bm Studio"])
        keys("\x1b[A", "\r", gap=0.6)
        screen(["Games"])
    finally:
        q.close()
    part = os.path.join(tmp, "part.img")
    with open(img, "rb") as f, open(part, "wb") as o:
        f.seek(2048 * 512)
        o.write(f.read())
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    saved = subprocess.run(["mtype", "-i", part, "::/CARTS/VILLAGE.BM"], capture_output=True, env=env).stdout
    models, _ = bmmesh.decode(dict(bmmesh.cart_sections(saved))[bmmesh.SEC_MESH])
    hero = [m for m in models if m["name"] == "hero"]
    assert hero and 8 <= len(hero[0]["faces"]) <= 200, [m["name"] for m in models]
    reds = sum(1 for f in hero[0]["faces"] if f[3] >> 16 > 150 and f[3] & 0xFF < 100)
    assert reds >= len(hero[0]["faces"]) // 2, reds                                  # the disc's red on the faces
    ys = [v[1] for v in hero[0]["verts"]]
    assert abs(max(ys) - 2) < 0.05 and abs(min(ys)) < 0.05, (min(ys), max(ys))
    shutil.rmtree(tmp, ignore_errors=True)
    print(f"picture: the cutout hero made on the console ({len(hero[0]['faces'])} triangles, red), saved; the service's message")


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
        screen(["Open in bm Studio"])
        keys("s", "s", "s", "s", "s", "s")      # Play, SDK, Code, Sound, Studio, Animator, Mesh
        screen(["Open in bm Mesh"])
        keys("\r")
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
        screen(["Open in bm Studio"])
        for _ in range(7):
            keys("s")                           # Play, SDK, Code, Sound, Studio, Animator, Mesh, Pixel
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
        screen(["Open in bm Studio"])
        for _ in range(7):
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


def test_overbit(b, opts):
    """M38: Overbit boots to its title (the sunset sky and the orange menu
    bar on screen), the keyboard takes it to the training range (first
    person: Rally's white cannons in the lower corners), J held fires the
    cannons; no Lua error, frame statistics on quit."""
    q = Qemu(b("kernel.img"), USB_KBD)

    def count(img, test):
        w, h, px = img
        return sum(test(*px[(y * w + x) * 3:(y * w + x) * 3 + 3]) for y in range(0, h, 4) for x in range(0, w, 4))

    def shot(name, ok=lambda img: True, tries=24):
        # QEMU shows the page being drawn (it ignores the flips) and a frame
        # takes long here: screendumps until one is whole
        for _ in range(tries):
            img = q.screendump()
            if ok(img):
                break
            time.sleep(0.3)
        if opts.shots:
            _save_png(img, os.path.join(opts.shots, f"overbit-{name}.png"))
        return img

    try:
        q.expect(MENU, timeout=30)
        time.sleep(0.5)
        with open(b("carts/overbit.bm"), "rb") as f:
            assert _upload(q, f.read())
        q.expect("overbit build", timeout=30)
        time.sleep(4)
        img = shot("title", lambda im: count(im, lambda r, g, b: r > 200 and 80 < g < 140 and b < 80) > 20)
        orange = count(img, lambda r, g, b: r > 200 and 80 < g < 140 and b < 80)
        sky = count(img, lambda r, g, b: b > 120 and r < 160 and b > g)
        print(f"     overbit title: orange {orange}, sky {sky}")
        assert orange > 20 and sky > 50, (orange, sky)
        sendkeys(q, "down")                     # TRAINING RANGE (under PLAY: CONTROL, PLAY ONLINE)
        time.sleep(0.5)
        sendkeys(q, "down")
        time.sleep(0.5)
        sendkeys(q, "spc")
        time.sleep(5)
        def corners(im):
            # the white cannons in the lower corners of the first-person view
            w, h, px = im
            n = 0
            for y in range(h * 3 // 4, h, 4):
                for x in list(range(0, w // 4, 4)) + list(range(w * 3 // 4, w, 4)):
                    r, g, bb = px[(y * w + x) * 3:(y * w + x) * 3 + 3]
                    n += r > 180 and g > 180 and bb > 180
            return n
        img = shot("range", lambda im: corners(im) > 10)
        white = corners(img)
        print(f"     overbit range: white in the lower corners {white}")
        assert white > 10, white
        with socket.socket(socket.AF_UNIX) as s:
            s.connect(q.mon_path)
            s.sendall(b"sendkey j 3000\n")       # hold J: Fusion Cannons
        time.sleep(1.5)
        shot("fire")
        time.sleep(2.5)
        q.send("q")
        out = q.expect("update+draw", timeout=20).decode(errors="replace")
        assert "stopped with an error" not in out, out
        assert '"Overbit"' in out, out[-400:]
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


def test_game_api(b, opts):
    """R10 and R11 on the ARM: the cartridge of make test-gameapi (the map's
    layers, the flags of the tiles, mflags, msize, mlayers, the sheet's
    named zones, cart_save / cart_load / cart_write keeping them, bmlib:
    collisions with the map, tweens, timers, scripts, particles, camera,
    states, text, saves, the 3D builder). Without the input script of
    bmhost it runs the checks of _init and of the first frame, then leaves:
    every one passed, and the map drawn by layer (the floor red, the
    platform green, the front layer's yellow tile)."""
    cart = b("gameapi-test.bm")
    if not os.path.exists(cart):
        subprocess.run(["make", "-s", cart], check=True, cwd=os.path.dirname(os.path.abspath(__file__)) + "/..")
    tmp = tempfile.mkdtemp(prefix="bm-gameapi-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    try:
        q.boot()
        assert _upload(q, open(cart, "rb").read())
        q.expect("received", timeout=30)
        time.sleep(2.0)
        w, h, px = img_ = q.screendump()
        text = screen_text(img_)
        assert any("game api" in l for l in text), "\n".join(text)

        def at(x, y, rgb):              # the screen's RGB565, widened without the low bits
            i = (y * w + x) * 3
            return all(abs(px[i + k] - (rgb >> (16 - 8 * k) & 255)) <= 8 for k in range(3))
        assert at(4, 164, 0xFF0000) and at(84, 124, 0x00FF00) and at(44, 44, 0xFFFF00), \
            [px[(y * w + x) * 3:(y * w + x) * 3 + 3] for x, y in ((4, 164), (84, 124), (44, 44))]
        if opts.shots:
            _save_png(img_, os.path.join(opts.shots, "game-api.png"))
        out = q.expect("checks passed", timeout=20).decode(errors="replace")
        m = re.search(r"gameapi: (\d+)/(\d+) checks passed", out)
        assert m and m.group(1) == m.group(2) and int(m.group(2)) > 100, out[-3000:]
        assert "FAIL" not in out and "stopped with an error" not in out, out[-3000:]
    finally:
        q.close()
        shutil.rmtree(tmp, ignore_errors=True)


# a square cartridge (256x256, in the middle of a 480x270 screen) lit by
# levels as in Dank Tomb: white everywhere, one lamp in the middle
SQUARE_CART = r"""
local l1 = 0
function _init()
  fades({ { 0xFFFFFF, 0x000000, 0x404040, 0x808080, 0xFFFFFF } })
end
function _draw()
  cls(0xFFFFFF)
  dark_begin(0)
  glow(128, 128, 60, 3)
  dark_end()
  if pad() & 1024 ~= 0 then l1 = 30 end
  if l1 > 0 then l1 = l1 - 1; print("L1 HELD", 8, 48, 0xFFFF00) end
  print("SQUARE " .. SCREEN_W .. "X" .. SCREEN_H, 8, 16, 0x00FF00)
end
"""


def test_square_lights(b, opts):
    """A 256x256 cartridge: shown in the middle of a 480x270 screen, black
    round it; the light by levels (fades, dark_begin, glow, dark_end): the
    lamp's middle as drawn, its edge dark; L1 from the serial line ('u');
    the dev kit's performance overlay ('p' from the serial line, F3)"""
    BX, BY = 112, 7                             # the 256x256 box in the 480x270 screen

    def box(img):
        w, h, px = img
        out = bytearray()
        for y in range(BY, BY + 256):
            out += px[(y * w + BX) * 3:(y * w + BX + 256) * 3]
        return 256, 256, bytes(out)

    q = Qemu(b("kernel.img"))
    try:
        q.expect(MENU, timeout=30)
        time.sleep(0.5)
        assert _upload(q, mkbm.pack(SQUARE_CART.encode(), title="square", res=(256, 256)))
        for _ in range(20):
            time.sleep(0.25)
            img = q.screendump()
            if img[0] == 480 and any("SQUARE 256X256" in l for l in screen_text(box(img))):
                break
        assert img[0] == 480 and img[1] == 270, img[:2]
        text = screen_text(box(img))
        assert any("SQUARE 256X256" in l for l in text), "\n".join(text)
        w, h, px = img
        for x, y in ((0, 0), (479, 269), (BX - 1, 128), (BX + 256, 128), (240, BY - 1), (240, BY + 256)):
            assert px[(y * w + x) * 3:(y * w + x) * 3 + 3] == b"\0\0\0", ("border", x, y)
        mid = px[((BY + 128) * w + BX + 128) * 3:((BY + 128) * w + BX + 128) * 3 + 3]
        edge = px[((BY + 240) * w + BX + 128) * 3:((BY + 240) * w + BX + 128) * 3 + 3]
        assert min(mid) > 200 and max(edge) < 40, (mid, edge)
        if opts.shots:
            _save_png(img, os.path.join(opts.shots, "square-lights.png"))
        q.send("u")                             # L1
        time.sleep(0.3)
        text = screen_text(box(q.screendump()))
        assert any("L1 HELD" in l for l in text), "\n".join(text)
        q.send("p")                             # the performance overlay: simple
        time.sleep(0.6)
        text = screen_text(box(q.screendump()))
        assert any("fps" in l and "ms" in l for l in text), "\n".join(text)
        q.send("p")                             # detailed: the frame's phases
        time.sleep(0.6)
        text = screen_text(box(q.screendump()))
        assert any("update" in l for l in text) and any("draw" in l for l in text), "\n".join(text)
        q.send("p")                             # off
        time.sleep(0.6)
        text = screen_text(box(q.screendump()))
        assert not any("fps" in l for l in text), "\n".join(text)
        q.send("q")
        out = q.expect("update+draw", timeout=10).decode(errors="replace")
        assert "stopped with an error" not in out, out
    finally:
        q.close()


SCREEN_CART = r"""
local cube, f, i = nil, 0, 0
local MODES = { { 640, 360 }, { 1920, 1080 }, { 320, 180 }, { 960, 540 } }
function _init()
  cube = mesh_cube(0xE06030)
  log("refused " .. tostring(screen(800, 600)))
end
function _update()
  f = f + 1
  if f % 40 == 0 and i < #MODES then
    i = i + 1
    screen(MODES[i][1], MODES[i][2])
  end
  if f % 40 == 2 then log(string.format("screen now %dx%d", SCREEN_W, SCREEN_H)) end
end
function _draw()
  cls(0x203050)
  camera3d(0, 1.2, -9, 0, -0.1, 60)
  zclear()
  draw3d(cube, 0, 0, 0, 0.3, 0.6, 0, 1)
  print("SCREEN " .. SCREEN_W .. "X" .. SCREEN_H, 0, 0, 0xFFFFFF)
end
"""


def test_reports(b, opts):
    """The reports (src/kernel/reports.c, 2026-10-04): Z in the monitor makes
    one of the log, saved as bm/reports/RPT00001.TXT with its header (kernel,
    branch, board, date, the long name with kernel and branch), the token
    masked where the log had it (the repository may be public); with a token
    but no network (QEMU) it waits on the card, and z says so; p (the
    render bench) makes its own of what it printed."""
    tmp = tempfile.mkdtemp(prefix="bm-reports-")
    img = os.path.join(tmp, "sd.img")
    cfg = os.path.join(tmp, "config.txt")
    with open(cfg, "w") as f:
        f.write("layout=us\nwifi_boot=0\ngithub_token=test-token-4242\n")
    mksd.build(img, [(cfg, "bm/config.txt"), (b("demo.bm"), "carts/game.bm")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    try:
        q.expect(MENU, timeout=30)
        time.sleep(0.5)
        q.send("q")
        q.expect(PROMPT)
        q.expect("> ")
        q.send("l")                             # the token printed: it is in the log now
        q.expect("lua> ")
        q.send("print('the token is ' .. 'test-token-' .. 4242)\r")
        q.expect("the token is test-token-4242")
        q.send("exit()\r")
        q.expect("> ")
        q.send("Z")
        out = q.expect("report: on the SD card: no network", timeout=20).decode(errors="replace")
        m = re.search(r"report: (nodate-[0-9a-f]{6}_log_\S+_\S+\.txt) saved as bm/reports/RPT00001\.TXT", out)
        assert m, out
        name = m.group(1)
        q.expect("> ")
        q.send("z")
        out = q.expect("waiting on the SD card", timeout=20).decode(errors="replace")
        assert "on the SD card: no network; 1 waiting" in out, out
        q.expect("> ")
        q.send("p")                             # the render bench: a report of its printout
        out = q.expect("RPT00002.TXT", timeout=120).decode(errors="replace")
        assert re.search(r"report: nodate-[0-9a-f]{6}_render_", out), out
        q.expect("> ")
        time.sleep(0.5)
    finally:
        q.close()
    part = os.path.join(tmp, "part.img")
    with open(img, "rb") as f, open(part, "wb") as o:
        f.seek(2048 * 512)
        o.write(f.read())
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    log = subprocess.run(["mtype", "-i", part, "::/BM/REPORTS/RPT00001.TXT"], capture_output=True, text=True,
                         env=env).stdout
    render = subprocess.run(["mtype", "-i", part, "::/BM/REPORTS/RPT00002.TXT"], capture_output=True, text=True,
                            env=env).stdout
    shutil.rmtree(tmp, ignore_errors=True)
    head = log.split("\n\n", 1)[0].splitlines()
    assert head[0] == "bm report" and "kind: log" in head and f"file: {name}" in head, log[:600]
    kernel = next(l[8:] for l in head if l.startswith("kernel: "))
    branch = next(l[8:] for l in head if l.startswith("branch: "))
    assert kernel and branch and "date: unknown (no network time)" in head, head
    assert name.endswith(f"_{kernel.lower()}.txt"), (name, kernel)
    assert "cartridge menu" in log.split("\n\n", 1)[1], "the log's body"
    assert "test-token-4242" not in log and "the token is ***************" in log, "the token masked"
    assert "kind: render" in render and "Rendering benchmark" not in render.split("\n\n", 1)[0], render[:400]
    assert "sprites" in render.split("\n\n", 1)[1], render[:800]


def test_menu_scale(b, opts):
    """menu_scale=3 in bm/config.txt (2026-10-04): the menu at 1920x1080,
    the same layout as at 640x360, every pixel 3x3 (the ARM enlarges it;
    the GPU will draw it there one day)."""
    tmp = tempfile.mkdtemp(prefix="bm-scale-")
    img = os.path.join(tmp, "sd.img")
    cfg = os.path.join(tmp, "config.txt")
    with open(cfg, "w") as f:
        f.write("layout=us\nmenu_scale=3\n")
    mksd.build(img, [(cfg, "bm/config.txt"), (b("demo.bm"), "carts/game.bm")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    try:
        q.expect(MENU, timeout=30)
        for _ in range(30):
            big = q.screendump()
            w, h, px = big
            small = bytes(px[((y * 3) * w + x * 3) * 3 + k] for y in range(h // 3) for x in range(w // 3)
                          for k in range(3))
            text = screen_text((w // 3, h // 3, small))
            if (w, h) == (1920, 1080) and "Settings" in text[1] and "bm native demo" in text[4]:
                break
            time.sleep(0.3)
        assert (w, h) == (1920, 1080), f"screen {w}x{h}"
        assert "Games" in text[1] and "Settings" in text[1] and "bm native demo" in text[4], "\n".join(text)
        # every 3x3 block one colour: the layout enlarged, not drawn again
        for y in range(0, 1080, 37 * 3):
            for x in range(0, 1920, 41 * 3):
                block = {px[((y + j) * w + x + i) * 3:((y + j) * w + x + i) * 3 + 3] for j in range(3)
                         for i in range(3)}
                assert len(block) == 1, f"the 3x3 block at {x},{y}: {block}"
        if opts.shots:
            _save_png(big, os.path.join(opts.shots, "menu-1080.png"))
    finally:
        q.close()
        shutil.rmtree(tmp, ignore_errors=True)


def test_screen_modes(b, opts):
    """screen(w, h): a cartridge changes its resolution between frames
    (640x360, 1920x1080, 320x180, 960x540): the screen has the new size,
    SCREEN_W/SCREEN_H say it, the 3D (the ARM's here) and the 2D fill it;
    a size not in the list is refused"""
    q = Qemu(b("kernel.img"))
    try:
        q.expect(MENU, timeout=30)
        time.sleep(0.5)
        assert _upload(q, mkbm.pack(SCREEN_CART.encode(), title="screens", res=(480, 270)))
        out = q.expect("refused false", timeout=30).decode(errors="replace")
        for w, h in ((640, 360), (1920, 1080), (320, 180), (960, 540)):
            q.expect(f"screen now {w}x{h}", timeout=60)
            for _ in range(20):
                img = q.screendump()
                top = (img[0], 16, img[2][:img[0] * 16 * 3])
                if img[:2] == (w, h) and any(f"SCREEN {w}X{h}" in l for l in screen_text(top)):
                    break
                time.sleep(0.3)
            assert img[:2] == (w, h), (img[:2], w, h)
            text = screen_text(top)
            assert any(f"SCREEN {w}X{h}" in l for l in text), text
            iw, ih, px = img
            orange = sum(1 for y in range(0, ih, 4) for x in range(0, iw, 4)
                         if px[(y * iw + x) * 3] > 150 and px[(y * iw + x) * 3 + 2] < 90)
            corner = px[((ih - 2) * iw + iw - 2) * 3:((ih - 2) * iw + iw - 2) * 3 + 3]
            print(f"     {w}x{h}: cube {orange} samples, corner {tuple(corner)}")
            assert orange > (iw // 4) * (ih // 4) // 40, orange
            assert tuple(corner) == (0x20, 0x30, 0x50) or abs(corner[2] - 0x50) < 8, corner
            if opts.shots:
                _save_png(img, os.path.join(opts.shots, f"screen-{w}x{h}.png"))
        q.send("q")
        out = q.expect("update+draw", timeout=20).decode(errors="replace")
        assert "stopped with an error" not in out, out
    finally:
        q.close()


FRAMESKIP_CART = r"""
local ups, draws, presses, held = 0, 0, 0, 0
local t0, u0, d0
function _init()
  log("frameskip was " .. frameskip(4) .. ", now " .. frameskip())
end
function _update()
  ups = ups + 1
  if btnp(4) then presses = presses + 1 end
  if btn(4) then held = held + 1 end
end
function _draw()
  draws = draws + 1
  local t = time()
  while time() - t < 0.04 do end        -- a frame that costs 40 ms
  cls(1)
  print("draws " .. draws .. " updates " .. ups, 8, 8, 7)
  if draws == 10 then t0, u0, d0 = time(), ups, draws log("frameskip measuring") end
  if draws == 60 then
    log(string.format("frameskip draws %d updates %d in %.2f s, stat15 %d, presses %d held %d",
                      draws - d0, ups - u0, time() - t0, stat(15), presses, held))
  end
end
"""


def test_frameskip(b, opts):
    """frameskip(n): a _draw that costs 40 ms, and the game's time still goes
    at 60 _update a second (up to 4 before each _draw; stat(15) says how
    many); a button pressed counts once in btnp() however many _update see
    it (btn() stays held)"""
    q = Qemu(b("kernel.img"))
    try:
        q.expect(MENU, timeout=30)
        time.sleep(0.5)
        assert _upload(q, mkbm.pack(FRAMESKIP_CART.encode(), title="frameskip"))
        out = q.expect("frameskip measuring", timeout=30).decode(errors="replace")
        assert "frameskip was 1, now 4" in out, out[-400:]
        q.send("j")                             # A, held 10 frames from the serial line
        out = q.expect("frameskip draws", timeout=60).decode(errors="replace")
        out += q.expect("\n", timeout=5).decode(errors="replace")
        m = re.search(r"frameskip draws (\d+) updates (\d+) in ([\d.]+) s, stat15 (\d+), presses (\d+) held (\d+)",
                      out)
        assert m, out[-400:]
        draws, ups, secs, last, presses, held = (int(m.group(1)), int(m.group(2)), float(m.group(3)),
                                                 int(m.group(4)), int(m.group(5)), int(m.group(6)))
        print(f"     {draws} draws, {ups} updates in {secs:.2f} s (game time {ups / 60:.2f} s), "
              f"last frame {last}, A pressed {presses}, held {held} updates")
        assert ups >= 2 * draws, (ups, draws)
        assert 2 <= last <= 4, last
        assert abs(ups / 60 - secs) < 0.25 * secs, (ups, secs)
        assert presses == 1 and held > 10, (presses, held)
        q.send("q")
        out = q.expect("update+draw", timeout=20).decode(errors="replace")
        assert "stopped with an error" not in out, out
    finally:
        q.close()


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
        if opts.shots:
            time.sleep(0.5)
            _save_png(q.screendump(), os.path.join(opts.shots, "sound-pattern.png"))
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


def test_room_bench(b, opts):
    """M33: the Texture Room benchmark (monitor R, Dev tab): the room at
    320x180 and then at 640x360 (the HUD on screen at both sizes), crates
    doubled until under 30 fps, a line per step and the summary; QEMU has
    no V3D, so only the ARM's cases and a line saying why."""
    q = Qemu(b("kernel.img"))
    try:
        q.boot()
        q.send("R")
        q.expect("Texture Room: crates doubled", timeout=10)
        seen = set()
        t0 = time.time()
        while len(seen) < 2 and time.time() - t0 < 120:
            w, h, px = q.screendump()
            hud = sum(all(abs(px[(y * w + x) * 3 + i] - (255, 224, 96)[i]) < 30 for i in range(3))
                      for y in range(0, 16, 2) for x in range(0, w, 2))
            if hud and (w, h) in ((320, 180), (640, 360)):
                seen.add((w, h))
            time.sleep(0.3)
        assert seen == {(320, 180), (640, 360)}, seen
        out = q.expect("Texture Room benchmark done", timeout=300).decode(errors="replace")
        plain = re.sub(r"\x1b\[[0-9;]*m", "", out)
        for case in ("ARM 320x180", "ARM 640x360"):
            m = re.search(re.escape(case) + r"\s+(\d+)\s+(\d+)\s+[\d.]+ ms\s+[\d.]+ fps", plain)
            assert m and int(m[1]) >= 8 and int(m[2]) > 100, f"{case}:\n{plain}"
        assert "GPU: none (no V3D answers" in plain, plain
        assert re.search(r"ARM 320x180\s+(<8|\d+ \(\d+\))\s+(<8|\d+ \(\d+\))", plain), plain
        assert "GPU 320x180" not in plain and "error" not in plain, plain
        q.expect("> ", timeout=10)
    finally:
        q.close()


def test_gpu_absent(b, opts):
    """M33: QEMU has no V3D: the GPU test stops at its first step and says
    why, and the monitor goes on."""
    q = Qemu(b("kernel.img"))
    try:
        q.boot()
        q.send("g")
        out = q.expect("GPU test \x1b[91mfailed", timeout=20).decode(errors="replace")
        assert "1 power on the 3D unit" in out, out
        assert "no V3D answers" in out, out
        q.expect("> ", timeout=10)
        q.send("i")
        q.expect("> ", timeout=10)
    finally:
        q.close()


def test_gpu3d_fallback(b, opts):
    """M33: gpu3d=1 in bm/config.txt on a machine without a V3D (QEMU): the
    game says why in the log and its 3D is drawn by the ARM as before."""
    tmp = tempfile.mkdtemp(prefix="bm-gpu3d-")
    img = os.path.join(tmp, "sd.img")
    cfg = os.path.join(tmp, "config.txt")
    with open(cfg, "w") as f:
        f.write("wifi_boot=0\ngpu3d=1\n")
    mksd.build(img, [(cfg, "bm/config.txt")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    try:
        q.expect(MENU, timeout=30)
        time.sleep(0.5)
        with open(b("texroom.bm"), "rb") as f:
            assert _upload(q, f.read())
        out = q.expect("bm: the 3D is drawn by the ARM as bm3d 0.2: ", timeout=20).decode(errors="replace")
        q.expect("no V3D answers", timeout=5)
        for _ in range(20):                     # the room, with its textures (after _init)
            time.sleep(0.5)
            w, h, px = q.screendump()
            cols = [tuple(px[(y * w + x) * 3:(y * w + x) * 3 + 3]) for y in range(0, h, 4) for x in range(0, w, 4)]
            if len(set(cols)) > 100:
                break
        assert len(set(cols)) > 100, len(set(cols))
        q.send("q")
        out = q.expect("update+draw", timeout=10).decode(errors="replace")
        assert "stopped with an error" not in out and "GPU 3D" not in out, out
    finally:
        q.close()
        shutil.rmtree(tmp, ignore_errors=True)


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
    times the network on this CPU; Esc closes the panel, Ctrl+Esc (Ctrl+\
    on the serial line) leaves. The tools open the same panel (require
    "assist") with F6."""
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
        k("\x1b", 1.0)                                     # Esc: nothing to go back to
        see(["bm assistant", "speed", "exit"])            # still here (Ctrl+Esc exits)
        k("\x1c", 0.5)                                     # Ctrl+\ (Ctrl+Esc): back to the monitor
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


RES480_CART = r"""
local m
function _init()
  for y = 0, 15 do for x = 0, 15 do sset(x, y, x < 8 and 0xFF0000 or 0x0000FF) end end
  m = mesh({ -1,-1,0, 1,-1,0, 1,1,0, -1,1,0 }, { 1,3,2,-1, 1,4,3,-1 },
           { 0,16, 16,0, 16,16,   0,16, 0,0, 16,0 })
end
local n = 0
function _update() n = n + 1 end
function _draw()
  cls(0x00FF00)
  zclear()
  camera3d(0, 0, -3)
  light3d(0, 0, -1, 1)
  draw3d(m, 0, 0, 0)
  rectfill(SCREEN_W - 8, SCREEN_H - 8, 8, 8, 0xFFFFFF)
  if n == 30 then
    log("res", SCREEN_W, SCREEN_H, string.format("%06x %06x %06x", pget(200, 135), pget(280, 135),
        pget(SCREEN_W - 1, SCREEN_H - 1)), stat(4))
  end
end
"""


def test_res_480(b, opts):
    """M33: a cartridge at 480x270 (4x on 1080p): the screen mode, 3D with
    textures (the z-buffer cleared by the DMA from the second frame on)."""
    q = Qemu(b("kernel.img"))
    try:
        q.boot()
        assert _upload(q, mkbm.pack(RES480_CART.encode(), title="480 test", res=(480, 270)))
        out = q.expect("res\t", timeout=15).decode(errors="replace")
        out += q.expect("\n").decode(errors="replace")
        assert "res\t480\t270\tff0000 0000ff ffffff\t2" in out, out
        w, h, px = q.screendump()
        assert (w, h) == (480, 270), (w, h)
        q.send("q")
        q.expect("> ", timeout=10)
    finally:
        q.close()


def test_code_completion(b, opts):
    """bm Code's word completion (src/ai/predict.lua): while a word is typed
    its rest appears in grey-blue and the status line says "Tab: word"; Tab
    writes it (green until the next key) with the words of where the cursor
    is: Lua in the code, Italian after "--", the questions to the assistant
    after "#entry:"; in the find prompt the tab's names; in the assistant's
    panel the question. Tab before any letter still indents."""
    q = Qemu(b("kernel.img"))

    def k(s, gap=0.05):
        for c in re.findall(r"\x1b\[[0-9]*[~A-Z]|\x1bO[A-Z]|.", s, re.S):
            q.send(c)
            time.sleep(gap)

    def see(words, tries=40):
        text = []
        for _ in range(tries):
            text = screen_text(q.screendump(), 6, 12)
            if all(any(w in l for l in text) for w in words):
                return text
            time.sleep(0.25)
        raise AssertionError(f"not on screen: {words}\n" + "\n".join(text))

    def coloured(rgb, tol=12):
        """the pixels of about that colour on the screen"""
        w, h, px = q.screendump()
        want = ((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255)
        n = 0
        for i in range(0, len(px), 3):
            if all(abs(px[i + j] - want[j]) <= tol for j in range(3)):
                n += 1
        return n

    GHOST, PRED = 0x6C8CC8, 0x50E0B0

    def shot(name):
        if opts.shots:
            _save_png(q.screendump(), os.path.join(opts.shots, f"code-complete-{name}.png"))
    try:
        q.boot()
        k("C")
        q.expect("code: ready", timeout=15)
        see(["keys"])
        k("\x14", 0.4)                                      # Ctrl+T: an empty tab
        k("f", 0.4)
        see(["  1 function", "Tab: function"])              # f + the grey-blue rest
        assert coloured(GHOST) > 20, "the suggestion in grey-blue"
        shot("ghost")
        k("\t", 0.4)
        text = see(["  1 function"])
        assert not any("Tab:" in l for l in text), "written: no suggestion left"
        assert coloured(PRED) > 20, "what Tab wrote, green"
        shot("written")
        k(" _upd", 0.1)
        see(["Tab: _update"])
        k("\t", 0.3)
        k("()\r", 0.1)
        k("-- muovi il gioc", 0.08)                          # a comment: Italian
        see(["Tab: gioco"])
        k("\t", 0.3)
        k("\r", 0.1)
        k("if bt", 0.08)
        see(["Tab: btnp"])
        k("\t", 0.3)
        see(["  1 function _update()", "  2   -- muovi il gioco", "  3   if btnp"])
        k("\r\r#entry: come faccio a sal", 0.06)           # a request: the questions
        see(["Tab: salvare"])
        # Tab before a word: still the indentation
        k("\x0c", 0.3)                                      # Ctrl+L: go to line
        k("3\r", 0.2)
        k("\x1b[H\x1b[H", 0.1)                              # Home twice: the first column
        k("\t", 0.3)
        see(["  3     if btnp"])
        # the find: the tab's names
        k("\x06", 0.4)                                      # Ctrl+F
        see(["Find:"])
        k("\b" * 20, 0.02)
        k("_up", 0.1)
        k("\t", 0.3)
        see(["Find:", "_update"])
        k("\r", 0.4)
        # the assistant: the question
        k("\x1b[17~", 0.6)                                  # F6
        see(["Assistant"])
        k("\x15", 0.2)                                      # Ctrl+U: an empty question
        k("co", 0.2)
        see(["? come"])
        k("\t", 0.4)
        k(" faccio a saltare", 0.05)
        see(["? come faccio a saltare", "Saltare con la gravit"])
        shot("assistant")
        k("\x1b", 0.6)
    finally:
        q.close()


def test_pad_typing(b, opts):
    """Typing with the pad (src/ai/padtype.lua) with a simulated DS4. Pad
    Typing from the Games tab, free writing in compose: up writes t and the
    prediction its syllable after the wait, up up quickly d, circle erases
    the syllable; L2 + R2 the numbers, triangle turns the digit, cross twice
    a full stop; Share the on-screen keyboard (cross writes its key) and
    back; R2 + cross a word; L1 + R1 held a new line; Start the pause. A
    text of the practice shows the next press. bm Code: Share turns the
    typing on (the status line says PAD) and off."""
    tmp = tempfile.mkdtemp(prefix="bm-padtype-")
    img = os.path.join(tmp, "sd.img")
    hcd = os.path.join(tmp, "BCM43430A1.hcd")
    with open(hcd, "wb") as f:
        f.write(bytes([0x4C, 0xFC, 4, 1, 2, 3, 4, 0x4E, 0xFC, 4, 0xFF, 0xFF, 0xFF, 0xFF]))
    pad, key = FakeDs4Chip.DS4, FakeDs4Chip.KEY
    cfg = os.path.join(tmp, "config.txt")
    with open(cfg, "w") as f:
        f.write(f"layout=it\nbt_pad=1c:66:6d:01:02:03 {key.hex()}\n")
    mksd.build(img, [(hcd, "bm/BCM43430A1.hcd"), (cfg, "bm/config.txt"),
                     (b("carts/typing.bm"), "carts/typing.bm")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"], mini_uart=True)
    q.mini_buf = b""
    HAT = {None: 8, "up": 0, "right": 2, "down": 4, "left": 6}
    SQU, CRO, CIR, TRI = 0x10, 0x20, 0x40, 0x80
    L1, R1, L2, R2, SHARE, START = 0x01, 0x02, 0x04, 0x08, 0x10, 0x20

    def tap(hat=None, face=0, sh=0, hold=0.12, gap=0.3):
        chip.report(HAT[hat] | face, shoulders=sh)
        time.sleep(hold)
        chip.report(0x08)
        time.sleep(gap)

    def double(hat=None, face=0, sh=0):         # two presses well inside the wait
        tap(hat, face, sh, 0.06, 0.06)
        tap(hat, face, sh, 0.06, 0.3)

    def see(words, gone=(), cw=8, ch=16, tries=40):
        text = []
        for _ in range(tries):
            text = screen_text(q.screendump(), cw, ch)
            if all(any(w in l for l in text) for w in words) and not any(g in l for l in text for g in gone):
                return text
            time.sleep(0.25)
        shot("fail")
        raise AssertionError(f"not on screen: {words} (or still: {gone})\n" + "\n".join(text))

    def row(n):                                  # a line of the free writing (x 16, y 32 + 16 n)
        r = screen_text(q.screendump())[2 + n][2:78].rstrip()
        return r[:-1].rstrip() if r.endswith("?") else r   # the cursor, when it is lit

    def wait_row(n, ok, what, tries=30):
        r = ""
        for _ in range(tries):
            r = row(n)
            if ok(r):
                return r
            time.sleep(0.2)
        shot("fail")
        raise AssertionError(f"{what}: [{r}]\n" + "\n".join(screen_text(q.screendump())))

    def shot(name):
        if opts.shots:
            _save_png(q.screendump(), os.path.join(opts.shots, f"padtype-{name}.png"))

    try:
        q.expect("(same pins, same speed)\r\n", timeout=30)
        chip = FakeDs4Chip(q.port)
        chip.buf, q.buf = q.buf, b""
        chip.init(reset_silent=False)
        _mini_expect(q, "cartridge menu")
        chip.reconnect(pad, key, 0x0B, (0x50, 0x51), 1)
        _mini_expect(q, "bt: controller 1c:66:6d:01:02:03 connected (player 1)")
        time.sleep(0.5)
        see(["Pad Typing"])
        tap(face=CRO)                            # the first cover of Games
        _mini_expect(q, "typing: ready", timeout=40)
        see(["Language", "< Italiano >", "Start with"])
        # the menu: the longest wait (600 ms, QEMU is slow), free writing
        tap("down")
        tap("down")
        for _ in range(5):
            tap("right")
        tap("down")
        tap("left")
        see(["< 600 ms >", "< free writing >"])
        shot("menu")
        tap(sh=START)
        _mini_expect(q, "typing: Italiano, free, compose, 600 ms")
        see(["compose", "free writing"])
        # up: t, and after the wait the prediction finishes its syllable
        tap("up", gap=1.2)
        r = wait_row(0, lambda s: s.startswith("T"), "up: T and its syllable")
        shot("syllable")
        tap(face=CIR, gap=0.5)
        wait_row(0, lambda s: s == "", "circle: the syllable goes")
        double("up")
        wait_row(0, lambda s: s.startswith("D"), "up up quickly: D")
        tap(face=CIR, gap=0.5)
        wait_row(0, lambda s: s == "", "circle: it goes")
        # L2 + R2: the numbers; circle there is 0, triangle turns the digit
        tap("up", sh=L2 | R2, gap=1.2)
        wait_row(0, lambda s: s == "1", "L2 + R2 + up: 1")
        tap(face=CIR, sh=L2 | R2)
        tap(face=TRI, gap=0.5)
        wait_row(0, lambda s: s == "19", "0 turned back: 9")
        double(face=CRO)                         # cross twice: a full stop and a space
        wait_row(0, lambda s: s == "19.", "cross twice: a full stop")
        # Share: the on-screen keyboard, its cross writes the key (q, a capital)
        tap(sh=SHARE, gap=0.5)
        see(["keyboard"])
        shot("keyboard")
        tap(face=CRO, gap=0.5)
        wait_row(0, lambda s: s.startswith("19. Q"), "the keyboard's key")
        tap(sh=SHARE, gap=0.5)
        see(["compose"])
        tap(face=CRO, sh=R2, gap=0.6)            # R2 + cross: the first word
        r = wait_row(0, lambda s: s.startswith("19. Q") and len(s) >= 8, "R2 + cross: a word")
        # L1 + R1 held: a new line, its first letter a capital
        chip.report(0x08, shoulders=L1 | R1)
        time.sleep(0.8)
        chip.report(0x08)
        time.sleep(0.3)
        tap("right", gap=1.2)
        wait_row(1, lambda s: s.startswith("N") or s.startswith("M"), "a new line, then right: N")
        shot("free")
        # Start: the pause; Menu, then the first text of the practice
        tap(sh=START, gap=0.5)
        see(["Pause", "Continue", "Next text"])
        for _ in range(3):
            tap("down")
        tap(face=CRO, gap=0.5)
        see(["Language", "< free writing >"])
        tap("right")
        see(["< text 1 of 8 >"])
        tap(sh=START)
        _mini_expect(q, "typing: Italiano, text 1, compose, 600 ms")
        see(["Ciao Marco", "next:"])
        shot("practice")
        chip.report(0x08, ps=1)                  # PS: back to bm's menu, the cart suspended
        _mini_expect(q, "update+draw")
        chip.report(0x08)
        time.sleep(0.3)
        q.mini.write(b"q")
        _mini_expect(q, "back to the monitor")

        # bm Code: Share turns the typing on, the numbers, Share off
        q.mini.write(b"C")
        _mini_expect(q, "code: ready", timeout=30)
        see(["keys"], cw=6, ch=12)
        q.mini.write(b"\x14")                    # Ctrl+T: an empty tab
        time.sleep(0.5)
        tap(sh=SHARE, gap=0.5)
        see(["PAD compose lua"], cw=6, ch=12)
        tap("up", sh=L2 | R2)
        tap("right", sh=L2 | R2)
        tap("down", sh=L2 | R2, gap=1.0)
        see(["  1 123"], cw=6, ch=12)
        shot("code")
        tap(sh=SHARE, gap=0.5)
        see(["  1 123"], gone=["PAD compose"], cw=6, ch=12)
    finally:
        q.close()


def test_editor(b, opts):
    """M15 and the SDK update (2026-10-04): the bm SDK opens on the project
    page (the suite's programs, the contents measured), F1 again the dev
    kit, Ctrl+N a project from a template (Platform 2D: code, sprites and
    map), the 2D and 3D pages, Save as, Ctrl+R tries it and the dev kit
    brings back the run's numbers (fps, ms, RAM, tokens); a game that stops
    with an error brings the SDK to the line; the card is still a clean
    FAT32 volume."""
    tmp = tempfile.mkdtemp(prefix="bm-ed-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [(b("carts/pong.bm"), "carts/pong.bm")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    F1, F2, F3, F4 = "\x1bOP", "\x1bOQ", "\x1bOR", "\x1bOS"

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

    def shot(name):
        if opts.shots:
            _save_png(q.screendump(), os.path.join(opts.shots, f"sdk-{name}.png"))
    try:
        q.boot()
        k("e")
        see("OPEN IN")                                      # the project page: the suite
        see("bm Animator")
        see("tokens")                                       # the contents, measured
        shot("project")
        k(F1, 0.5)                                          # F1 again: the dev kit
        see("DATA IN MEMORY")
        see("not tried yet")
        shot("devkit")
        k(F1, 0.3)
        k("\x0e", 0.5)                                      # Ctrl+N: the templates
        see("new project from a template")
        k("\x1b[B", 0.3)                                    # Platform 2D
        see("run and jump on the map")
        shot("templates")
        k("\r", 0.5)
        see("new project (Platform 2D)")
        k(F3, 0.5)                                          # the sprites: the hero in cell 1
        see("SPRITES")
        shot("sprites")
        k(F3, 0.5)                                          # F3 again: the map
        see("MAP")
        shot("map")
        k(F4, 0.5)                                          # 3D: no models yet
        see("no 3D models in this project yet")
        shot("3d")
        k(F2, 0.5)
        time.sleep(3)                                       # (the message of the new project goes)
        see("line 1/")
        shot("code")
        k("\x1b", 0.5)                                      # code -> the menu
        see("Exit bm SDK")
        for _ in range(4):
            k("\x1b[B", 0.25)                               # down to "Save as..."
        k("\r")
        see("file name")
        k("\r")                                             # MYGAME.BM
        see("saved /carts/MYGAME.BM")
        k("\x1b", 0.5)                                      # back to the code
        k("\x12", 1)                                        # Ctrl+R: try it
        for _ in range(40):                                 # the game is on (slow hosts)
            _, text = settled_screen(q, lambda i, t: True, tries=1)
            if not any("saved /carts/MYGAME.BM" in l or "line 1/" in l for l in text):
                break
            time.sleep(0.25)
        time.sleep(2)
        k("q")                                              # and back to the SDK
        out = q.expect('bm: "Platform 2D"', timeout=20).decode(errors="replace")
        out += q.expect("tokens", timeout=10).decode(errors="replace")
        assert "stopped with an error" not in out, out
        assert re.search(r"dev kit: Lua peak \d+ KiB, data \d+ KiB", out), out
        see("back from the game:")                          # the run's numbers
        shot("back")
        k(F1, 0.5)
        k(F1, 0.5)
        see("RAM ")                                         # the dev kit: the last try
        see(" fps")
        shot("devkit-run")
        k(F2, 0.5)
        time.sleep(3)
        k("\x1b[H", 0.3)
        for ch in "error('boom')\r":
            k(ch, 0.05)
        k("\x12")
        out = q.expect("main.lua:1: boom", timeout=20).decode(errors="replace")
        time.sleep(3)
        _, text = settled_screen(q, lambda i, t: any("the game stopped" in l for l in t))
        assert any("the game stopped" in l for l in text), "\n".join(text)
        shot("error")
        k("\x1b", 0.4)
        k("\x1b[A", 0.3)                                    # Exit bm SDK
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


def test_sdk_layers(b, opts):
    """R11 in the bm SDK: a new Platform 2D project (its ground solid and its
    planks platforms by the tiles' flags), 4 on the sprite page sets flag 4
    of the cell, Shift+L on the map page adds a layer and space places a tile
    on it, Ctrl+S saves the cartridge with its LAYERS and FLAGS sections,
    read back by bm.h's rules."""
    tmp = tempfile.mkdtemp(prefix="bm-sdl-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    F3 = "\x1bOR"

    def k(s, gap=0.2):
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
        see("OPEN IN")
        k("\x0e", 0.5)                                      # Ctrl+N: the templates
        see("new project from a template")
        k("\x1b[B", 0.3)                                    # Platform 2D
        k("\r", 0.5)
        see("new project (Platform 2D)")
        k(F3, 0.5)                                          # the sprites, on cell 1
        see("SPRITES")
        k("4")
        see("flag 4 on")
        k(F3, 0.5)                                          # the map
        see("layer 1/1 main")
        k("L")
        see("map layer 2/2: layer2")
        k("\x1b[C", 0.2)
        k(" ")                                              # tile 1 at (1, 0) of layer 2
        k("\x13", 1)                                        # Ctrl+S: no name yet, Save as
        see("file name")
        k("\r")
        see("saved /carts/")
    finally:
        q.close()
    try:
        part = os.path.join(tmp, "part.img")
        with open(img, "rb") as f, open(part, "wb") as o:
            f.seek(2048 * 512)
            o.write(f.read())
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
        names = subprocess.run(["mdir", "-b", "-i", part, "::/CARTS"], capture_output=True, env=env,
                               text=True).stdout.split()
        assert names, "no cartridge saved"
        data = subprocess.run(["mtype", "-i", part, names[0]], capture_output=True, env=env).stdout
        assert data[:8] == b"BMCART\x00\x00", data[:16]
        secs = {}
        for i in range(data[17]):
            t, off, size, _ = struct.unpack_from("<IIII", data, 128 + i * 16)
            secs[t] = data[off:off + size]
        lay = secs[12]
        w, h, n = struct.unpack_from("<HHH", lay, 0)
        assert (w, h, n) == (256, 256, 2), (w, h, n)
        assert lay[8:24].rstrip(b"\0") == b"main" and lay[24:40].rstrip(b"\0") == b"layer2", lay[8:40]
        cells = lay[8 + 2 * 16:]
        assert struct.unpack_from("<H", cells, 1 * 2)[0] == 1, "tile 1 at (1, 0) of layer 2"
        per, rows = struct.unpack_from("<HH", secs[13], 0)
        fl = secs[13][4:]
        assert per == 32 and fl[1] == 16 and fl[2] == 1 and fl[6] == 2, (per, rows, list(fl[:8]))
        assert "lib.step(hero)" in secs[1].decode(), "the template's code on bmlib"
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def test_sdk_suite(b, opts):
    """The SDK update (2026-10-04): the bm SDK opens Studio Village (Ctrl+O),
    its 3D page lists the models and draws them, i writes the code for one,
    the assistant's model (F6) joins the project and is saved with it; 3 on
    the project page opens bm Studio on the file, whose menu has "Back to
    bm SDK"; the SDK comes back on the project."""
    tmp = tempfile.mkdtemp(prefix="bm-sdk-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [(b("carts/village.bm"), "carts/village.bm")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    F1, F4, F6 = "\x1bOP", "\x1bOS", "\x1b[17~"

    def k(s, gap=0.2):
        q.send(s)
        time.sleep(gap)

    def see(word, tries=60):
        for _ in range(tries):
            _, text = settled_screen(q, lambda i, t: any(word in l for l in t), tries=2)
            if any(word in l for l in text):
                return text
            time.sleep(0.25)
        raise AssertionError(f"not on screen: {word}\n" + "\n".join(text))

    def shot(name):
        if opts.shots:
            _save_png(q.screendump(), os.path.join(opts.shots, f"sdk-{name}.png"))
    try:
        q.boot()
        k("e", 1)
        see("OPEN IN")
        k("\x0f", 0.5)                                      # Ctrl+O: the cartridges
        see("open a cartridge")
        k("\r", 1)
        see("Studio Village")
        see("8: ")                                          # the models, on the project page
        shot("village")
        k(F4, 1)
        see("MODELS 8")
        see("vertices")
        shot("3d-models")
        k("i", 0.5)                                         # the code for the model
        see("draw3d")
        shot("model-code")
        k(F4, 0.5)
        k(F6, 1)                                            # the assistant: a 3D recipe
        see("Assistant")
        for ch in "cane":
            k(ch, 0.1)
        time.sleep(2)
        shot("assistant-mesh")
        k("\r", 1)
        see("the assistant's")
        see("MODELS 9")
        shot("3d-assistant")
        k("\x13", 1)                                        # Ctrl+S
        see("saved /carts/VILLAGE.BM")
        k(F1, 0.5)
        k("3", 1)                                           # bm Studio on the project
        see("TOOLS")                                        # its build page
        time.sleep(2)
        k("\x1b", 1)                                        # its menu: the way back
        see("Back to bm SDK")
        shot("studio-back")
        k("\x1b[A", 0.4)
        k("\x1b[A", 0.4)
        k("\r", 1)
        see("back from bm Studio")
        see("OPEN IN")
        shot("back-from-studio")
        q.send("\x1c")                                      # Ctrl+\ (Ctrl+Esc): leave
        time.sleep(1)
        q.send("\x1c")
        q.expect("> ", timeout=20)
    finally:
        q.close()
    try:
        part = os.path.join(tmp, "part.img")
        with open(img, "rb") as f, open(part, "wb") as o:
            f.seek(2048 * 512)
            o.write(f.read())
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
        saved = subprocess.run(["mtype", "-i", part, "::/CARTS/VILLAGE.BM"], capture_output=True, env=env).stdout
        secs = dict(bmmesh.cart_sections(saved))
        import struct
        assert struct.unpack("<H", secs[bmmesh.SEC_MESH][:2])[0] == 9, "the assistant's model saved"
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
        # the quad rows give the cost of one pixel
        for name in ("quad 320x180 flat", "quad 320x180 no z", "quad 320x180 Gouraud",
                     "quad 320x180 texture"):
            assert re.search(re.escape(name) + r"\s+\S+\s+\S+\s+[\d.]+ q\s+\d+ ns/px", plain), \
                f"{name} missing:\n{plain}"
        assert re.search(r"irq \d+\.\d% \(\d+/s", plain), plain
        # QEMU has no V3D: no GPU rows, and the line says why
        assert "GPU rows: none (no V3D answers" in plain and "GPU spheres" not in plain, plain
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
        if opts.shots:
            _save_png(q.screendump(), os.path.join(opts.shots, "nano8-sixlets2.png"))
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
    ap.add_argument("--kernel7", action="store_true",
                    help="kernel7.img (Pi Zero 2 W) in raspi2b instead of kernel.img in raspi0")
    ap.add_argument("--shard", default="", metavar="K/N",
                    help="only group K of N, about the same time each (the CI's machines)")
    opts = ap.parse_args()
    if opts.shard:
        m = re.fullmatch(r"(\d+)/(\d+)", opts.shard)
        if not m or not 1 <= int(m.group(1)) <= int(m.group(2)):
            ap.error("--shard K/N with 1 <= K <= N")
    global KERNEL7, ZERO2
    KERNEL7 = opts.kernel7
    if KERNEL7:                         # make image puts it on the card too
        os.environ["ZERO2"] = "1"
        ZERO2 = True

    def b(name):
        if KERNEL7 and name == "kernel.img":
            name = "kernel7.img"
        return os.path.join(opts.build, name)

    tests = [(n, f) for n, f in globals().items()
             if n.startswith("test_") and opts.filter in n]
    if opts.shard:
        k, n = map(int, opts.shard.split("/"))
        tests = shard(tests, k, n)
        print(f"group {k} of {n}: {len(tests)} tests, about "
              f"{sum(SLOW.get(t, 5) for t, _ in tests) / 60:.0f} min on the CI", flush=True)
    failed = skipped = 0
    for name, fn in tests:
        skip = KERNEL7 and next((why for t, why in BCM2835_ONLY.items() if name.startswith(t)), None)
        if skip:
            print(f"SKIP {name} (kernel7.img: needs {skip})", flush=True)
            skipped += 1
            continue
        t0 = time.time()
        try:
            fn(b, opts)
            print(f"PASS {name} ({time.time() - t0:.1f}s)", flush=True)
        except Exception:
            failed += 1
            print(f"FAIL {name}", flush=True)
            traceback.print_exc()
            sys.stdout.flush()
    print(f"\n{len(tests) - skipped - failed}/{len(tests) - skipped} passed"
          + (f" ({skipped} skipped)" if skipped else ""))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
