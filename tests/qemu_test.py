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
sys.path.insert(0, os.path.join(HERE, "studio"))
sys.path.insert(0, os.path.join(HERE, "ai"))
import anim_figure  # noqa: E402
import bm_load  # noqa: E402
import bmmesh  # noqa: E402
import bmres  # noqa: E402
import glbfix  # noqa: E402
import mkbm  # noqa: E402
import mksd  # noqa: E402

QEMU = os.environ.get("QEMU", "qemu-system-arm")
REF_DIR = os.path.join(HERE, "ref")
PROMPT = b"type 'h' for help"
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
    "test_pad_no_pointer": "a Bluetooth pad (the radio chip)",
    "test_chainloader": "the serial chainloader (ARMv6)",
}

# --shard K/N: the tests split into N groups of about the same time (the CI
# runs them on N machines at once). Seconds on the CI (2026-10-05) of the
# tests over 20 s; the others count 5. A test missing here only makes the
# groups less even.
SLOW = {
    "test_web_api": 83,
    "test_chainloader": 70,
    "test_home_ui": 66, "test_pad_typing": 48,
    "test_editor": 39, "test_code_completion": 38,
    "test_menu_tabs": 31,
    "test_sdk_suite": 29, "test_monitor_line": 29, "test_mouse_cart": 23, "test_market": 23,
    "test_code_editor": 23, "test_code_modules": 45, "test_update": 22, "test_video_player": 25, "test_video_rec": 60, "test_video_rec_rle": 60, "test_video_rec_drops": 40, "test_bench_stop": 35,
    "test_nano8": 20, "test_crash_report": 100, "test_sd_games_bm": 15, "test_editor_mouse": 40,
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
        shutil.rmtree(self.tmp, ignore_errors=True)


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

def copy_yes(q, game, copy, timeout=30):
    """a tool saving a game: the console's question of the editable copy
    (runtime.c copy_question), answered yes from the serial line (A)"""
    q.expect(f"bm: {game} is read only: save an editable copy as {copy}?", timeout=timeout)
    q.send("j")
    q.expect("bm: copied", timeout=timeout)       # a big game copies slowly on a busy runner


def test_boot_banner(b, opts):
    q = Qemu(b("kernel.img"))
    try:
        out = q.boot()
        board = b"board a21041" if KERNEL7 else b"board 920092"   # raspi2b: a Pi 2 B
        for s in (b"bm\x1b[0m kernel", board,
                  b"double buffer on", b"sd: no card", b"usb: nothing attached"):
            assert s in out, f"missing {s!r} in boot log"
        hz = int(re.search(rb"measured (\d+) Hz", out).group(1))
        assert 900 <= hz <= 1100, f"timer IRQ rate {hz} Hz"  # QEMU host jitter
        plain = re.sub(rb"\x1b\[[0-9;]*m", b"", out).decode(errors="replace")
        for s in ("MMU+caches on", "IRQ on: timer 1000 Hz"):
            assert s in plain, f"missing {s!r} in boot log"
        text = "\n".join(screen_text(q.screendump()))
        assert all(len(l) < 80 for l in text.splitlines()), "boot output wraps:\n" + text
        q.send("i")
        out = q.expect("uptime")
        assert b"MMU/caches     : on" in out, out
        q.send("m")
        q.expect("KB in use")
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
        # grows past the 64 MB Lua limit: must fail cleanly, not crash
        q.send("t = {} for i = 1, 1000 do t[i] = ('x'):rep(1 << 20) .. i end\r")
        out = q.expect("lua> ", timeout=60)
        assert b"not enough memory" in out, out
        q.send("t = nil collectgarbage() print('alive', 6 * 7)\r")
        assert b"alive\t42" in q.expect("lua> ", timeout=20)
    finally:
        q.close()




def bar_icons(img):
    """The status icons at the right of the menu bar (M27): the column
    spans [x0, x1) with light pixels in rows 8-39, right of the tabs (Settings,
    the last, ends at x 352)."""
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
    (top, bottom) of its light pixels at x 620, y 96-300, or None. The bar
    is 4 pixels wide, centred in the right margin: SBAR_X in
    src/kernel/menu_ui.c (618 with six covers of 88 and a gap of 6)."""
    ys = [y for y in range(96, 300) if sum(pixel(img, 620, y)) > 600]
    return (ys[0], ys[-1] + 1) if ys else None


def tabs_lit(img):
    """Which tabs of the menu bar are on their light pill (M27, Market since
    M25): Market, Games, Dev, Settings, from a pixel of the pill left of
    each name. The Market waits off the screen at the left (2026-10-04): on
    it the tabs are where they were, elsewhere 7 columns to the left."""
    if sum(pixel(img, 20, 24)) > 600:
        return ["Market"]
    places = (("Games", 44), ("Dev", 116), ("Settings", 172))
    return [name for name, x in places if sum(pixel(img, x, 24)) > 600]


def img_tabs(q, want=None):
    """the tabs lit on the screen (a few tries for `want`: a frame may be half drawn)"""
    for _ in range(8):
        lit = tabs_lit(q.screendump())
        if want is None or lit == want:
            break
        time.sleep(0.2)
    return lit


def settled_screen(q, ok, tries=15):
    """QEMU shows page 0 even while it is being drawn (it ignores the
    virtual offset), so a screendump can catch a frame half drawn: retry
    until `ok(img, text)` holds. The retries are 0.1 s apart: back to back
    they can all land in the same long frame of a loaded runner. `ok` must
    ask for everything the caller asserts afterwards (a half-drawn frame
    can have the title and miss the line below it)."""
    for n in range(tries):
        img = q.screendump()
        text = screen_text(img)
        if ok(img, text):
            return img, text
        if n + 1 < tries:
            time.sleep(0.1)
    return img, text


def wait_menu(q, timeout=15.0):
    """The cartridge menu once it is drawn (its tab row says "Games"): the
    serial line "cartridge menu" comes before the first frame, later on a
    busy runner, and a key typed before the menu runs is lost. Writes the
    screen text and a PNG to build/diag/ when the menu never shows."""
    deadline = time.time() + timeout
    while True:
        img = q.screendump()
        text = screen_text(img)
        if "Games" in text[1]:
            return img, text
        if time.time() > deadline:
            os.makedirs("build/diag", exist_ok=True)
            _save_png(img, "build/diag/menu-not-drawn.png")
            with open("build/diag/menu-not-drawn.txt", "w") as f:
                f.write("\n".join(text))
            raise AssertionError("the menu is not drawn after %.0f s:\n%s" % (timeout, "\n".join(text)))
        time.sleep(0.2)


# the keys test: a hint on the bottom row that changes at the first direction key
KEYS_CART = r"""
local auto = true
function _update()
  if btn(0) or btn(1) or btn(2) or btn(3) then auto = false end
end
function _draw()
  cls(0x203040)
  print(auto and "attract" or "arrows/wasd, space", 0, SCREEN_H - 24, 0xA0A0A0)
end
"""


def test_bm_keys(b, opts):
    q = Qemu(b("kernel.img"))
    try:
        q.boot()
        assert _upload(q, mkbm.pack(KEYS_CART.encode(), title="keys test"))
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
        assert _upload(q, mkbm.pack(KEYS_CART.encode(), title="keys test"))     # uploaded: it plays
        time.sleep(1.5)
        q.buf = b""
        sendkeys(q, "esc")
        time.sleep(1.0)
        q.buf += q.port.read(0.3)
        assert b"update+draw" not in q.buf, "Esc left the game"
        sendkeys(q, "ctrl-esc")
        out = q.expect("update+draw", timeout=10).decode(errors="replace")
        m = re.search(r'keys test" (\d+) frames', out)
        assert m and int(m[1]) < 300, out
        q.expect("> ")

        # monitor command and Lua REPL typed on the Italian layout:
        # shift-8 = '(', shift-] = '*', shift-2 = '"', ';' key = 'Ã²' (CP437 0x95)
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
    mksd.build(img, [(b("carts/astrowing.bm"), "bm/demo.bm")])
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
    mksd.build(img, [(b("carts/astrowing.bm"), "Il mio gioco lungo.bm"),
                     (b("carts/pong.bm"), "bm/demo2.bm"),
                     (os.path.join(HERE, "..", "README.md"), "README.md")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    try:
        out = q.boot().decode(errors="replace")
        assert "sd: SD card (sdhost), FAT32, 127 MB, label BMSD; 2 cartridges" in out, out
        q.send("f")
        out = q.expect("\"Pong\"\r\n").decode(errors="replace")
        assert "Il mio gioco lungo.bm" in out and "/bm/demo2.bm" in out, out
        q.expect("> ")
        q.send("M")
        q.expect("cartridge menu")
        wait_menu(q)
        want = ("Games", "Astro Wing")
        _, text = settled_screen(q, lambda i, t: all(any(w in l for l in t) for w in want))
        screen = "\n".join(text)
        for s_ in want:
            assert s_ in screen, screen
        q.send("d")                           # the next cover: its title (the file is in the console list above)
        want = ("Pong",)
        _, text = settled_screen(q, lambda i, t: all(any(w in l for l in t) for w in want))
        screen = "\n".join(text)
        assert all(w in screen for w in want), screen
        q.send("a")
        want = ("Astro Wing",)   # the cover is back before Enter starts it
        _, text = settled_screen(q, lambda i, t: all(any(w in l for l in t) for w in want))
        assert all(any(w in l for l in text) for w in want), "\n".join(text)
        q.send("\r")                          # SD cartridges come first, by title
        q.expect("playing Il mio gioco lungo.bm", timeout=10)
        q.expect("bm: loaded", timeout=10)    # the system's splash, then the game
        time.sleep(1.0)
        q.send("q")
        out = q.expect("update+draw", timeout=15).decode(errors="replace")
        assert '"Astro Wing"' in out, out
        wait_menu(q)                          # the menu is back (the game is suspended) before the keys
        q.send("d")                           # next: /bm/demo2.bm
        want = ("Pong",)
        _, text = settled_screen(q, lambda i, t: all(any(w in l for l in t) for w in want))
        assert all(any(w in l for l in text) for w in want), "\n".join(text)
        q.send("\r")
        _, text = settled_screen(q, lambda i, t: any("Close Astro Wing?" in l for l in t))
        assert any("Close Astro Wing?" in l for l in text), "\n".join(text)
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
        shutil.rmtree(tmp, ignore_errors=True)


def test_sd_games_bm(b, opts):
    """The Pi menu scans /bm and the root, not /carts (25 Oct 2026): a game in
    /bm or in the root is listed, a game left in /carts is not."""
    tmp = tempfile.mkdtemp(prefix="bm-sdbm-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [(b("carts/pong.bm"), "carts/pong.bm"),
                     (b("carts/snake.bm"), "snake.bm"),
                     (b("carts/astrowing.bm"), "bm/rgbgame.bm")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    try:
        out = q.boot().decode(errors="replace")
        assert "label BMSD; 2 cartridges" in out, out
        q.send("f")
        out = q.expect("> ").decode(errors="replace")
        assert "/snake.bm" in out and "/bm/rgbgame.bm" in out and "/carts/pong.bm" not in out, out
    finally:
        q.close()
        shutil.rmtree(tmp, ignore_errors=True)


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
    """M21: leaving a game from the menu keeps it frozen in memory (a star
    on its cover); A on it resumes from the same frame; starting another
    cartridge asks first (B keeps it, A closes it and frees the memory)."""
    tmp = tempfile.mkdtemp(prefix="bm-susp-")
    img = os.path.join(tmp, "sd.img")
    cart = os.path.join(tmp, "counter.bm")
    with open(cart, "wb") as f:
        f.write(mkbm.pack(COUNTER_CART.encode(), title="AAA counter"))
    mksd.build(img, [(cart, "bm/counter.bm"), (b("carts/astrowing.bm"), "bm/zdemo.bm")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    try:
        q.expect(MENU, timeout=30)
        time.sleep(0.5)
        q.send("\r")
        q.expect("counter start", timeout=10)
        q.expect("frame 120", timeout=10)
        q.send("q")
        q.expect('"AAA counter" suspended', timeout=10)
        _, text = settled_screen(q, lambda i, t: not any("Playing" in l for l in t))
        assert not any("Playing" in l for l in text), "\n".join(text)
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
    mksd.build(img, [(cart, "bm/online.bm")])
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
function _init() save({ n = 1 }) save({ level = 2 }, 2) log("saver start") end
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
    big = os.path.join(tmp, "BIG.DAT")           # 1.5 MB: wide enough to show in the bar of the SD map
    with open(big, "wb") as f:
        f.write(bytes(3 << 19))
    notes = os.path.join(tmp, "NOTES.TXT")
    with open(notes, "wb") as f:
        f.write(b"x" * 3000)
    mksd.build(img, [(saver, "bm/saver.bm"), (victim, "bm/Un gioco da cancellare.bm"),
                     (big, "bme/PROJ.BME"), (big, "bm/DATA.DAT"), (notes, "bm/NOTES.TXT")])
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
        # (the "Open in" rows, the editable copy and Publish are not in the panel: the editors open the projects)
        opts_text = screen(["AAA saver", "Resume", "Close the game"])
        for gone_row in ("Open in", "Make an editable copy", "Publish to the Market"):
            assert gone_row not in opts_text, opts_text
        shot("options")
        keys("ww")                              # up from the first row: the last ones
        screen(["Delete the save data", "Records and progress start again"])
        screen(["Save data", "bytes in 2 slots"])     # save(t) and save(t, 2): R12
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

        # Close the game closes the panel too: the covers again, with no Resume to come back to
        text = screen(["Games", "AAA saver", "Options"])    # (the bar shows key chips only)
        assert "Close the game" not in text and "Resume" not in text, text

        # the other cartridge leaves the SD card
        keys("d")
        keys("x")
        screen(["BBB delete me", "Play"])
        keys("w")                               # up from the first: the last rows, the file too
        screen(["Delete from the SD card", "/bm/Un gioco da cancellare.bm"])
        keys("\r")
        screen(["Delete BBB delete me?", "leaves the SD card"])
        keys("q")                               # no: nothing happens
        time.sleep(0.5)
        keys("\r")
        screen(["Delete BBB delete me?"])
        keys("\r")
        q.expect("menu: deleted /bm/Un gioco da cancellare.bm", timeout=10)
        text = screen(["AAA saver"])
        assert "BBB" not in text, text

        # settings: the same sections as the RGB30's (settings.c); the
        # keyboard layout changes and is saved; the submenus
        keys("4")
        net = "Network" if KERNEL7 else "WiFi and network"     # raspi2b: a Pi 2 B, Ethernet only
        screen(["Settings", "Controllers", "Controls", net, "Screen", "Sound", "Updates"])    # System is below the fold with the Controls row
        shot("settings")
        keys("\r")                              # Controllers
        screen(["Settings > Controllers", "Player 1", "keyboard / USB", "Bluetooth keyboard",
                "Mouse"])
        keys("w")                               # up from the first: round to the last rows
        screen(["Pair a mouse", "Test the buttons", "Keyboard layout", "Button icons", "Scan the USB again",
                "Forget all controllers"])
        keys("www")                             # the keyboard layout
        _, text = settled_screen(q, lambda i, t: any("Keyboard layout" in l and ("Italian" in l or "US" in l) for l in t))
        line = next(l for l in text if "Keyboard layout" in l)
        before = "Italian" if "Italian" in line else "US"
        keys("\r")                              # Enter changes it (Settings has no left/right on a choice)
        after = "US" if before == "Italian" else "Italian"
        screen([after])
        keys("\r")                              # Enter again: back as it was
        screen([before])
        keys("q")
        keys("s")
        keys("\r")                              # Controls (CONTROLS-1; the RGB30 has the test of its panels)
        screen(["Settings > Controls", "Scroller games", "Top-down games", "Stick games", "Pause and menus", "Reset all"])
        keys("sss")
        keys("\r")                              # Pause and menus (D49): the Pi menus keep A and B, so only Pause is a row
        text = screen(["Settings > Controls > Pause and menus", "Pause"])
        assert "Cancel" not in text, text
        keys("q")
        keys("q")
        keys("s")
        keys("\r")
        if KERNEL7:
            screen(["Settings > Network", "Ethernet", "no cable", "port 3333"])
        else:
            screen(["Settings > WiFi and network", "Network", "none saved", "Connect to a network",
                    "port 3333"])               # Connect right after State
            keys("w")                           # the list scrolls to its last row
            screen(["Connect at boot", "Test the connection"])
            keys("w")
            screen(["Connect at boot", "On"])
        keys("q")
        keys("s")
        keys("\r")                              # Screen
        text = screen(["Settings > Screen", "Menu size", "360p (ARM)"])
        # 26 Oct 2026: the 3D of the games and the 3D on the ARM are hidden keys too (defaults: GPU, fast)
        for name in ("3D of the games", "3D on the ARM"):
            assert name not in text, f"{name!r} is a hidden key, not a Settings row: {text}"
        # the rows of drawing, anti-aliasing, textures, impostors and the overlay are hidden keys on the Pi
        # (0a): no row; config.txt still sets them
        for name in ("Game drawing", "3D anti-aliasing", "3D textures", "3D impostors", "Performance overlay"):
            assert name not in text, f"{name!r} is a hidden key, not a Settings row: {text}"
        # SYS-9: the performance flags are hidden keys too, with no row (still read from config.txt)
        hidden_rows = ("3D vertices", "3D frame queue", "3D frame shown", "2D over the 3D", "3D job memory",
                       "3D pixel shaders", "3D draw order", "3D corners")
        for name in hidden_rows:
            assert name not in text, f"{name!r} is a hidden key, not a Settings row: {text}"
        keys("s")
        screen(["Test pattern", "HDMI colour bars"])
        keys("q")
        keys("s")
        keys("\r")                              # Sound
        screen(["Settings > Sound", "Volume", "10 / 10"])
        keys("s")                               # the page starts on the Sound status row: down to the volume
        keys("\r")                              # the volume: Enter goes round (10, 0, 1, ...), saved
        screen(["0 / 10"])
        for _ in range(10):
            keys("\r")
        screen(["10 / 10"])
        keys("s")                               # the sound style: hi-fi, 8-bit (M46), saved
        screen(["Sound style", "Hi-fi"])
        keys("\r")
        screen(["8-bit"])
        keys("\r")
        screen(["Hi-fi"])
        keys("s")                               # the depth (2026-10-10): 24, 32, 16 and back, saved
        screen(["Bit depth", "24-bit", "Each sample: 16, 24 or 32"])
        keys("\r")
        screen(["32-bit"])
        keys("\r")
        screen(["16-bit"])
        keys("\r")
        screen(["24-bit"])
        keys("s")
        screen(["Test the sound", "HDMI sound status"])
        keys("q")
        keys("s")
        keys("\r")
        screen(["Settings > Updates", "Version", "Check for updates", "not checked"])
        keys("q")
        keys("s")
        keys("\r")
        screen(["Settings > Reports", "On the SD card", "none waiting", "Send the reports", "Report the log"])
        keys("q")
        keys("ss")                              # past Screen recorder (it changes on Enter)
        keys("\r")
        screen(["Settings > System", "Version", "Board", "SD card", "FAT32"])
        shot("system")
        # D09-5g: SD map (the scan runs in the menu's free time), its bar and "Other files"
        down = 0
        for down in range(1, 14):
            keys("s")
            _, t = settled_screen(q, lambda i, t: any("What is on the card" in l for l in t), tries=2)
            if any("What is on the card" in l for l in t):
                break
        else:
            raise AssertionError("SD map row never selected")
        keys("\r")
        screen(["Settings > System > SD map", "SD card", "Kernel", "bm (games)", "bme (projects)", "glb (models)", "bmm (models)"])
        for _ in range(8):                      # the panel shows five rows: the cursor scrolls down to Scan
            keys("s")
        screen(["Free", "Scan"])
        img_, text = settled_screen(q, lambda i, t: any("Scan" in l and "done" in l for l in t), tries=300)
        assert any("Scan" in l and "done" in l for l in text), "\n".join(text)
        for _ in range(12):                     # the folders follow Scan
            keys("s")
            _, t = settled_screen(q, lambda i, t: any("/bme" in l for l in t), tries=2)
            if any("/bme" in l for l in t):
                break
        screen(["/bm", "/bme", "files"])
        for _ in range(16):                     # back up until the bar row is on the screen
            keys("w")
            _, t = settled_screen(q, lambda i, t: any("Kernel" in l for l in t), tries=2)
            if any("Kernel" in l for l in t) and any("SD card" in l and "SD map" not in l for l in t):
                break
        screen(["SD card", "Kernel", "bm (games)"])
        img_ = q.screendump()
        text = screen_text(img_)
        row = next(i for i, l in enumerate(text) if "SD card" in l and "SD map" not in l)
        want = {"bm": (0x61, 0xAF, 0xEF), "bme": (0x98, 0xC3, 0x79), "free": (0x4A, 0x50, 0x60)}
        seen = {pixel(img_, x, row * 16 + 8) for x in range(24 * 8, img_[0])}
        for name, c in want.items():
            assert any(all(abs(a - b2) <= 8 for a, b2 in zip(c, got)) for got in seen), \
                f"bar colour {name} {c} not on the bar row: {sorted(seen)[:12]}"
        shot("sdmap")
        for _ in range(14):                     # "Other files" is the last row of the panel
            keys("s")
            _, t = settled_screen(q, lambda i, t: any("Files the menu cannot show" in l for l in t), tries=2)
            if any("Files the menu cannot show" in l for l in t):
                break
        keys("\r")
        screen(["Settings > System > SD map > Other files", "NOTES.TXT", "2 KB"])
        keys("q")
        keys("q")                               # back to System, the cursor on SD map
        for _ in range(down):
            keys("w")
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
        screen(["bm SDK"])
        keys("d")                               # the covers' names are on pictures: the pill
        screen(["bm Sound"])
        keys("d")
        screen(["bm Studio"])
        keys("d")
        screen(["bm Animator"])
        keys("d")
        screen(["bm Mesh"])
        keys("d")
        screen(["bm Pixel"])
        keys("d")
        screen(["Code"])
        keys("d")
        screen(["Assistant"])
        keys("d")
        screen(["Monitor"])
        shot("dev")
        keys("d")
        screen(["Lua"])     # the last tool of the tab: the utilities are in Settings
        keys("a")                               # the monitor, as a tool
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
        carts = subprocess.run(["mdir", "-i", part, "::/BM"], capture_output=True,
                               text=True, env=env).stdout
        assert "saver" in carts.lower() and "cancellare" not in carts, carts
        saves = subprocess.run(["mdir", "-i", part, "::/BM/SAVE"], capture_output=True,
                               text=True, env=env).stdout
        assert not re.search(r"^[0-9A-F]{8}\s+(SAV|S02)", saves, re.M), saves
        cache = subprocess.run(["mtype", "-i", part, "::/BM/SDMAP.TXT"], capture_output=True,
                               text=True, env=env).stdout
        assert cache.startswith("sdmap 2"), cache[:60]
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
        assert f"FAT32, 63 MB, label BM; {len(games)} cartridges" in out, out
        time.sleep(0.5)
        want = ("Pong", "Snake", "Star Shooter", "Chaos Kitchen", "nano8")
        titles = want + ("Astro Wing", "Titan Clash", "Overbit", "Yharnam", "Pad Typing")

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
        q.send("q")
        q.expect(PROMPT)
        q.expect("> ")
        q.send("f")
        out = q.expect('Star Shooter"\r\n').decode(errors="replace")
        assert "/bm/pong.bm" in out and "/bm/shooter.bm" in out, out
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
    mksd.build(img, [(cfg, "bm" + "33/config.txt"), (old, "bm/old.bm")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    try:
        out = q.boot().decode(errors="replace")
        assert "layout us" in out, out
        q.send("f")
        out = q.expect('"Old header"').decode(errors="replace")
        assert "/bm/old.bm" in out, out
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
    mksd.build(img, [(b("carts/snake.bm"), "bm/snake.bm")])
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
        assert saves.lower().count(".sav") == 1, saves
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


WRITER_CART = r"""
function _init()
  local code = { lua = "function _draw() cls(0) end", title = "written" }
  for _, p in ipairs({ "/kernel.img", "/bm/config.txt", "/bm/../kernel.bm", "/bm/sub/x.bm",
                       "/bm/.x.bm", "notes.txt", "/bm/ok.bm", "ok2.bm" }) do
    local ok, err = cart_save(p, code)
    log("save", p, ok, err)
  end
  local ok, err = cart_save("/bm/ok.bm", code)   -- made by this run: it can change it
  log("again", ok, err)
  ok, err = cart_write("ok2.bm", { lua = "x" })
  log("change", ok, err)
  ok, err = cart_write("/bm/other.bm", { lua = "x" })  -- there before: another game
  log("other", ok, err)
  ok, err = picture3d("ready", "meshy")
  log("picture", ok, err)
  ok, err = cart_write("/kernel.img", { lua = "x" , from = "/bm/ok.bm" })
  log("write", ok, err)
  ok, err = cart_put_audio("/bm/config.txt", nil)
  log("audio", ok, err)
  quit()
end
"""


LATER_CART = r"""
function _init()
  local ok, err = cart_write("/bm/ok.bm", { lua = "x" })   -- made by a run before
  log("later", ok, err)
  quit()
end
"""


def test_cart_write_limits(b, opts):
    """M25 (Market): a cartridge from the SD card or the Market writes only
    new .bm files in /bm, and changes only the ones it made in the same
    run (not one that is there: another game; nor its own in a later run);
    the kernel, the settings and other folders are refused, and so are the
    picture services with the user's keys (the tools built into the kernel
    still save anywhere and use them)."""
    tmp = tempfile.mkdtemp(prefix="bm-wlim-")
    img = os.path.join(tmp, "sd.img")
    cfg = os.path.join(tmp, "config.txt")
    with open(cfg, "w") as f:
        f.write("layout=us\nwifi_boot=0\n")
    other = os.path.join(tmp, "other.bm")
    with open(other, "wb") as f:
        f.write(mkbm.pack(b"function _draw() end", title="other"))
    mksd.build(img, [(cfg, "bm/config.txt"), (other, "bm/other.bm")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    try:
        q.boot()
        assert _upload(q, mkbm.pack(WRITER_CART.encode(), title="writer"))
        out = q.expect("audio\t", timeout=20)
        out += q.expect("\n")
        text = out.decode(errors="replace").replace("\r", "")
        for p in ("/kernel.img", "/bm/config.txt", "/bm/../kernel.bm", "/bm/sub/x.bm",
                  "/bm/.x.bm", "notes.txt"):
            assert f"save\t{p}\tfalse\t{p}: a cartridge writes only .bm and .bme files in /bm" in text, text
        assert "save\t/bm/ok.bm\ttrue" in text, text
        assert "save\tok2.bm\ttrue" in text, text
        assert "again\ttrue" in text and "change\ttrue" in text, text
        assert "other\tfalse\t/bm/other.bm: a cartridge cannot change a file that is there" in text, text
        assert "picture\tnil\tthe picture services are for bm's tools only" in text, text
        assert "write\tfalse\t/kernel.img: a cartridge writes only" in text, text
        assert "audio\tfalse\t/bm/config.txt: a cartridge writes only" in text, text
        q.expect("> ", timeout=10)
        assert _upload(q, mkbm.pack(LATER_CART.encode(), title="later"))
        out = q.expect("later\t", timeout=20) + q.expect("\n")
        assert b"later\tfalse\t/bm/ok.bm: a cartridge cannot change" in out, out
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
        cart_dir = subprocess.run(["mdir", "-b", "-i", part, "::/BM"], capture_output=True,
                                  text=True, env=env).stdout.upper()
        assert "OK.BM" in cart_dir and "OK2.BM" in cart_dir, cart_dir
        cfg_txt = subprocess.run(["mtype", "-i", part, "::/BM/CONFIG.TXT"], capture_output=True,
                                 text=True, env=env).stdout
        assert "wifi_boot=0" in cfg_txt, cfg_txt
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


GLB3D_CART = r"""
local function show(tag, m, err)
  if not m then log(tag, "nil", err) return end
  log(tag, m.nv, m.nf, tostring(m.textured), #m.record, m.flat and #m.flat or 0, m.texture and #m.texture or 0)
end

function _init()
  show("box", glb3d("/glb/box.glb"))
  show("small", glb3d("/glb/box.glb", { name = "tiny", height = 4, faces = 8, sheet = 64 }))
  show("sheet", glb3d("/glb/box.glb", { sheet = 4 }))
  show("missing", glb3d("/glb/none.glb"))
  show("text", glb3d("/glb/notes.glb"))
  show("grid", glb3d("/glb/grid.glb"))
  show("huge", glb3d("/glb/huge.glb"))
  quit()
end
"""


def test_glb3d(b, opts):
    """APP9-a: glb3d(path, {name=, height=, faces=, sheet=}) reads a .glb from the
    SD card (read only) and gives the model table of cutout3d, or nil and a message:
    the box with its texture, the options, a missing file, a file that is not a .glb,
    the format's limits (65535 triangles: refused; the reducer is in the host test) and the
    file size limit of 16 MB."""
    tmp = tempfile.mkdtemp(prefix="bm-glb3d-")
    img = os.path.join(tmp, "sd.img")
    try:
        files = {"box.glb": glbfix.build("png"), "notes.glb": b"just some text, not a glTF file",
                 "grid.glb": glbfix.grid(200), "huge.glb": bytes((16 << 20) + 4096)}
        cfg = os.path.join(tmp, "cfg")
        with open(cfg, "w") as f:
            f.write("layout=us\nwifi_boot=0\n")
        listing = [(cfg, "bm/config.txt")]
        for name, data in files.items():
            path = os.path.join(tmp, name)
            with open(path, "wb") as f:
                f.write(data)
            listing.append((path, "glb/" + name))
        mksd.build(img, listing)
        q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
        try:
            q.boot()
            assert _upload(q, mkbm.pack(GLB3D_CART.encode(), title="glb3d"))
            out = q.expect("huge\t", timeout=120) + q.expect("\n")
            text = out.decode(errors="replace").replace("\r", "")
            rows = {}
            for line in text.split("\n"):
                f = line.split("\t")
                if len(f) > 1:
                    rows[f[0].split()[-1]] = f[1:]
            box = rows["box"]
            assert box[:3] == ["13", "18", "true"], text
            assert int(box[3]) == 24 + 13 * 12 + 18 * 24, text            # the record: header, vertices, faces
            assert int(box[4]) == int(box[3]) and int(box[5]) == 256 * 256 * 4, text
            small = rows["small"]
            assert small[2] == "true" and int(small[1]) <= 8 and int(small[5]) == 64 * 64 * 4, text
            assert rows["sheet"][0] == "nil" and "sheet: 8 to 1024" in rows["sheet"][1], text
            assert rows["missing"][0] == "nil" and "/glb/none.glb" in rows["missing"][1], text
            assert rows["text"][0] == "nil" and "not a .glb" in rows["text"][1], text
            assert rows["grid"][0] == "nil" and "more than 65535 triangles" in rows["grid"][1], text
            assert rows["huge"][0] == "nil" and "limit is 16 MB" in rows["huge"][1], text
            q.expect("> ", timeout=10)
        finally:
            q.close()
        part = os.path.join(tmp, "part.img")
        with open(img, "rb") as f, open(part, "wb") as o:
            f.seek(2048 * 512)
            o.write(f.read())
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
        names = subprocess.run(["mdir", "-b", "-i", part, "::/GLB"], capture_output=True, text=True, env=env).stdout.upper()
        assert "BOX.GLB" in names and len(names.split()) == 4, names     # nothing written in /glb
        same = subprocess.run(["mtype", "-i", part, "::/GLB/BOX.GLB"], capture_output=True, env=env).stdout
        assert same == files["box.glb"], "the .glb is not changed"
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


RES_LUA = r"""
local M = %(mesh)s
local SHEET = string.pack("<I2I2", 8, 8) .. string.rep("\200\100\50\255", 64)
local function show(tag, ...)
  local t = table.pack(...)
  for i = 1, t.n do t[i] = tostring(t[i]) end
  log(tag, table.unpack(t, 1, t.n))
end
local function whole(tag, r, err)
  if not r then show(tag, "nil", err) return end
  local s = r.sections
  show(tag, r.kind, r.name, r.author, s[8] == M, s[9] and #s[9], s[2] and #s[2], s[5] and #s[5], s[10] and #s[10])
end
%(body)s
"""

RES_GAME_BODY = r"""
function _init()
  show("gw", res_write("/bmm/g.bmm", 1, { sections = { [8] = M } }))
  whole("gr", res_read("/other/host.bmm"))
  log("gdone")
end
"""

RES_TOOL_BODY = r"""
function _init()
  show("w1", res_write("/bmm/tri.bmm", 1, { sections = { [8] = M } }))
  whole("r1", res_read("/bmm/tri.bmm"))
  show("again", res_write("/bmm/tri.bmm", 1, { sections = { [8] = M } }))
  show("w2", res_write("/bmm/tri.bmm", "model", { sections = { [8] = M, [2] = SHEET }, replace = true,
                                                  name = "Tri Two", author = "me" }))
  whole("r2", res_read("/BMM/TRI.BMM"))
  for i, p in ipairs({ "/bmm/Tri.bmm", "/carts/x.bmm", "/bmm/toolongname.bmm", "/bmm/a/b.bmm", "/bmm/x.bm",
                       "/bmm/../x.bmm", "/bmm/.bmm", "/bmm/sp ace.bmm", "/bmm" }) do
    show("path" .. i, res_write(p, 1, { sections = { [8] = M } }))
  end
  show("trunc", res_write("/bmm/t1.bmm", 1, { sections = { [8] = M:sub(1, #M - 1) } }))
  show("nomesh", res_write("/bmm/t2.bmm", 1, { sections = {} }))
  show("kind", res_write("/bmm/t3.bmm", 2, { sections = { [8] = M } }))
  show("sec6", pcall(res_write, "/bmm/t4.bmm", 1, { sections = { [8] = M, [6] = "x" } }))
  show("sheets", res_write("/bmm/t5.bmm", 1, { sections = { [8] = M, [2] = SHEET, [5] = SHEET } }))
  show("badsheet", res_write("/bmm/t6.bmm", 1, { sections = { [8] = M, [2] = "abc" } }))
  show("badanim", res_write("/bmm/t7.bmm", 1, { sections = { [8] = M, [9] = "abcdefgh" } }))
  whole("host", res_read("/other/host.bmm"))
  whole("badcrc", res_read("/other/bad.bmm"))
  whole("game", res_read("/other/game.bm"))
  whole("none", res_read("/other/none.bmm"))
  whole("dir", res_read("/other"))
  log("tdone")
end
"""


def test_res_write(b, opts):
    """APP9-b: res_write(path, kind, {sections=, name=, author=, replace=}) makes a model
    file (BMRES) in /bmm, res_read(path) gives it back. For a game: res_write is refused and
    res_read works. For a tool (the SD card's sdk.bm runs as bm SDK): a new file, the file read
    back, no overwrite without replace, replace, a lower case name only, the MESH section
    checked, one sheet, the bytes of the file (the ones scripts/bmres.py makes), nothing else
    written in /bmm."""
    tmp = tempfile.mkdtemp(prefix="bm-reswrite-")
    img = os.path.join(tmp, "sd.img")
    try:
        mesh = bmmesh.encode([{"name": "tri", "verts": [(0, 0, 0), (1, 0, 0), (0, 1, 0)],
                               "faces": [(0, 1, 2, 0x336699, None)]}])
        bones = [{"name": "root", "parent": -1, "head": (0, 0, 0), "tail": (0, 1, 0)}]
        clip = {"name": "idle", "length": 1.0, "keys": [{"t": 0.0, "pose": [{"q": (0, 0, 0, 1), "t": (0, 0, 0)}]}]}
        anim = bmmesh.encode_anim([("tri", bones, [0, 0, 0], [clip])])
        host = bmres.dump(bmres.File(bmres.MODEL, "Host Model", "ann", [[8, mesh], [9, anim]]))
        broken = bytearray(host)
        broken[-1] ^= 0xFF
        sheet = struct.pack("<HH", 8, 8) + bytes([200, 100, 50, 255]) * 64

        def cart(body):
            return mkbm.pack((RES_LUA % {"mesh": _lua_bytes(mesh), "body": body}).encode(), title="reswrite")

        files = {"bm/sdk.bm": cart(RES_TOOL_BODY), "other/host.bmm": host, "other/bad.bmm": bytes(broken),
                 "other/game.bm": mkbm.pack(b"x = 1", title="game")}
        cfg = os.path.join(tmp, "cfg")
        with open(cfg, "w") as f:
            f.write("layout=us\nwifi_boot=0\n")
        listing = [(cfg, "bm/config.txt")]
        for name, data in files.items():
            path = os.path.join(tmp, name.replace("/", "_"))
            with open(path, "wb") as f:
                f.write(data)
            listing.append((path, name))
        mksd.build(img, listing)

        def rows_of(out):
            rows = {}
            for line in out.decode(errors="replace").replace("\r", "").split("\n"):
                f = line.split("\t")
                if len(f) > 1:
                    rows[f[0].split()[-1]] = f[1:]
            return rows

        # a game: no writing, reading is allowed
        q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
        try:
            q.boot()
            assert _upload(q, mkbm.pack((RES_LUA % {"mesh": _lua_bytes(mesh), "body": RES_GAME_BODY}).encode(),
                                        title="resgame"))
            out = q.expect("gdone", timeout=60)
            text = out.decode(errors="replace")
            rows = rows_of(out)
            assert rows["gw"][0] == "nil" and "res_write is for bm's tools only" in rows["gw"][1], text
            assert rows["gr"][:3] == ["1", "Host Model", "ann"] and rows["gr"][3] == "true", text
        finally:
            q.close()

        # a tool
        q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
        try:
            q.expect(MENU, timeout=30)
            time.sleep(0.5)
            q.send("3")                                           # Dev: the tools, bm SDK first
            settled_screen(q, lambda i, t: any("bm SDK" in l for l in t), tries=40)
            q.send("\r")
            out = q.expect("tdone", timeout=120)
            text = out.decode(errors="replace").replace("\r", "")
            rows = rows_of(out)
            assert rows["w1"] == ["true"], text
            assert rows["r1"][:4] == ["1", "tri", "", "true"] and rows["r1"][4:] == ["nil"] * 4, text
            assert rows["again"][0] == "nil" and "is there already" in rows["again"][1], text
            assert rows["w2"] == ["true"], text
            assert rows["r2"][:4] == ["1", "Tri Two", "me", "true"], text
            assert rows["r2"][4:] == ["nil", str(len(sheet)), "nil", "nil"], text
            for i in range(1, 10):
                m = rows[f"path{i}"]
                assert m[0] == "nil" and ("a model goes to /bmm/NAME.bmm" in m[1] or (i == 9 and "bmm" in m[1])), text
            assert rows["trunc"][0] == "nil" and "broken MESH section" in rows["trunc"][1], text
            assert rows["nomesh"][0] == "nil" and "needs its MESH section" in rows["nomesh"][1], text
            assert rows["kind"][0] == "nil" and "only kind 1" in rows["kind"][1], text
            assert rows["sec6"][0] == "false" and "a model file holds" in rows["sec6"][1], text
            assert rows["sheets"][0] == "nil" and "one sheet only" in rows["sheets"][1], text
            assert rows["badsheet"][0] == "nil" and "sheet" in rows["badsheet"][1], text
            assert rows["badanim"][0] == "nil" and "broken ANIM section" in rows["badanim"][1], text
            assert rows["host"][:4] == ["1", "Host Model", "ann", "true"], text
            assert rows["host"][4] == str(len(anim)), text
            assert rows["badcrc"][0] == "nil" and "CRC mismatch" in rows["badcrc"][1], text
            assert rows["game"][0] == "nil" and "not a resource file" in rows["game"][1], text
            assert rows["none"][0] == "nil" and "/other/none.bmm" in rows["none"][1], text
            assert rows["dir"][0] == "nil" and "a directory" in rows["dir"][1], text
        finally:
            q.close()
        part = os.path.join(tmp, "part.img")
        with open(img, "rb") as f, open(part, "wb") as o:
            f.seek(2048 * 512)
            o.write(f.read())
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
        names = subprocess.run(["mdir", "-b", "-i", part, "::/BMM"], capture_output=True, text=True, env=env).stdout.upper()
        assert "TRI.BMM" in names and len(names.split()) == 1, names          # nothing else written in /bmm
        got = subprocess.run(["mtype", "-i", part, "::/BMM/TRI.BMM"], capture_output=True, env=env).stdout
        want = bmres.dump(bmres.File(bmres.MODEL, "Tri Two", "me", [[8, mesh], [2, sheet]]))
        assert got == want, "the file is not the one scripts/bmres.py makes"
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


WEB_FIX = os.path.join(os.path.dirname(os.path.abspath(__file__)), "web", "fixtures")
WEBP_FIX = os.path.join(os.path.dirname(os.path.abspath(__file__)), "webp", "fixtures")


def _lua_bytes(data):
    """A Lua string literal with every byte escaped."""
    return '"' + "".join("\\%03d" % b for b in data) + '"'


def _web_fixture(path):
    with open(path, "rb") as f:
        return f.read()


WEB_API_CART = r"""
local JPG, PROG, PNG, WEBPLL, WEBPLY = %(jpg)s, %(prog)s, %(png)s, %(webpll)s, %(webply)s

local function hx(v) return v and string.format("%%06x", v) or "nil" end

function _init()
  -- image_load: JPEG, the size and three pixels
  local im, err = image_load(JPG, 0, 0)
  log("jpg", im and im.w, im and im.h, err)
  log("jpgpx", hx(sget(2, 10)), hx(sget(16, 10)), hx(sget(28, 5)))
  -- cut to maxw x maxh, placed away from the corner
  im, err = image_load(JPG, 100, 100, 16, 10)
  log("jpgcut", im and im.x, im and im.y, im and im.w, im and im.h, err, hx(sget(102, 103)), sget(116, 103) == nil)
  -- what does not fit the sheet is cut
  im, err = image_load(JPG, 250, 250)
  log("jpgedge", im and im.w, im and im.h, err)
  im, err = image_load(JPG, 256, 0)
  log("jpgout", im, err)
  im, err = image_load(PROG, 0, 0)
  log("prog", im, err)
  -- PNG: the opaque and the transparent pixels
  im, err = image_load(PNG, 40, 0)
  log("png", im and im.w, im and im.h, err)
  log("pngpx", hx(sget(40, 0)), hx(sget(43, 3)), sget(52, 3) == nil)
  -- WebP: lossless (exact pixels), lossy (size)
  im, err = image_load(WEBPLL, 0, 40)
  log("webpll", im and im.w, im and im.h, err)
  log("webpllpx", hx(sget(0, 40)), hx(sget(10, 50)), hx(sget(63, 87)))
  im, err = image_load(WEBPLY, 100, 40)
  log("webply", im and im.w, im and im.h, err)
  -- not a picture, a handle that is not there
  im, err = image_load("this is not a picture", 0, 0)
  log("junk", im, err)
  im, err = image_load(77, 0, 0)
  log("noid", im, err)
  -- the picture is a part of the sheet that sspr draws
  local ok = pcall(sspr, 0, 40, 64, 48, 10, 10)
  log("sspr", ok)

  -- file_write: the sandbox
  log("fw1", file_write("hello.txt", "hi"))
  log("fw2", file_write("bm/web/model.glb", string.rep("x", 100000)))
  log("fw3", file_write("/bm/web/hello.txt", "hi again"))
  for _, p in ipairs({ "../x.txt", "/kernel.img", "/bm/config.txt", "game.bm", "x.BME", "x.b16", "sub/a.txt",
                       "bm/web/sub/a.txt", "toolongname.txt", ".hid", "a/../b.txt", "" }) do
    local r, e = file_write(p, "no")
    log("fwno", p, r, e)
  end
  log("fwid", file_write("x.txt", 12345))
  local r, e = file_write("big.bin", string.rep("a", 9 * 1024 * 1024))
  log("fwbig", r, e)
  -- the folder keeps at most 128 files (hello.txt and model.glb are two of them)
  local n = 0
  for i = 1, 130 do
    local ok2, e2 = file_write(string.format("f%%03d.txt", i), "x")
    if not ok2 then log("fwmax", n, e2) break end
    n = n + 1
  end

  -- web_get: a request that cannot start never touches the network
  log("wg1", web_get("http://example.org/"))
  log("wg2", web_get("https://exa mple.org/"))
  log("wg3", web_get("https://example.org/#frag"))
  log("wg4", web_get("https://example.org/"))
  log("wp", web_poll(99))
  log("wi", web_info(99))
  log("wb", web_body(99))
  log("wc", web_cancel(99))
  log("end")
  quit()
end
"""


def test_web_api(b, opts):
    """bm3D 7.x web API for a browser cart: image_load (JPEG, PNG, WebP into the sprite sheet),
    file_write (only a bare 8.3 name in bm/web, never .bm/.bme/.b16, capped, 128 files) and the
    web_get address rules. The QEMU machine has no network: web_get stops at "no network" (the
    request itself is tested on the PC by make test-webapi)."""
    import zlib
    tmp = tempfile.mkdtemp(prefix="bm-webapi-")
    img = os.path.join(tmp, "sd.img")
    cfg = os.path.join(tmp, "config.txt")
    with open(cfg, "w") as f:
        f.write("layout=us\nwifi_boot=0\n")
    mksd.build(img, [(cfg, "bm/config.txt")])
    fx = {
        "jpg": _web_fixture(os.path.join(WEB_FIX, "block_32x24.jpg")),
        "prog": _web_fixture(os.path.join(WEB_FIX, "block_prog.jpg")),
        "png": _web_fixture(os.path.join(WEB_FIX, "alpha_16x8.png")),
        "webpll": _web_fixture(os.path.join(WEBP_FIX, "lossless_64x48.webp")),
        "webply": _web_fixture(os.path.join(WEBP_FIX, "lossy_q75_64x48.webp")),
    }
    src = WEB_API_CART % {k: _lua_bytes(v) for k, v in fx.items()}
    ref = zlib.decompress(_web_fixture(os.path.join(WEBP_FIX, "lossless_64x48.ref.z")))

    def rgb24(x, y):                    # the reference pixel as the sheet gives it back (RGB565 widened)
        o = (y * 64 + x) * 3
        r, g, bl = ref[o] >> 3, ref[o + 1] >> 2, ref[o + 2] >> 3
        return "%02x%02x%02x" % ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (bl << 3) | (bl >> 2))

    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    try:
        q.boot()
        assert _upload(q, mkbm.pack(src.encode(), title="webapi"))
        out = q.expect("end", timeout=400)
        text = out.decode(errors="replace").replace("\r", "")
        q.expect("> ", timeout=20)
    finally:
        q.close()

    def line(tag):
        m = [l for l in text.split("\n") if l.startswith(tag + "\t")]
        assert m, (tag, text[-3000:])
        return m[-1].split("\t")[1:]

    def near(hexv, rgb, tol=24):
        v = int(hexv, 16)
        return all(abs(((v >> s) & 255) - c) <= tol for s, c in ((16, rgb[0]), (8, rgb[1]), (0, rgb[2])))

    assert line("jpg") == ["32", "24", "nil"], line("jpg")
    px = line("jpgpx")
    assert near(px[0], (255, 0, 0)) and near(px[1], (0, 200, 0)) and near(px[2], (0, 0, 255)), px
    cut = line("jpgcut")
    assert cut[:5] == ["100", "100", "16", "10", "nil"] and near(cut[5], (255, 0, 0)) and cut[6] == "true", cut
    assert line("jpgedge") == ["6", "6", "nil"], line("jpgedge")
    out_of = line("jpgout")
    assert out_of[0] == "nil" and "outside the sprite sheet" in out_of[1], out_of
    prog = line("prog")
    assert prog[0] == "nil" and "progressive" in prog[1], prog
    assert line("png") == ["16", "8", "nil"], line("png")
    pp = line("pngpx")
    assert near(pp[0], (255, 0, 255)) and near(pp[1], (255, 255, 0)) and pp[2] == "true", pp
    assert line("webpll") == ["64", "48", "nil"], line("webpll")
    assert line("webpllpx") == [rgb24(0, 0), rgb24(10, 10), rgb24(63, 47)], (line("webpllpx"), rgb24(0, 0))
    assert line("webply") == ["64", "48", "nil"], line("webply")
    junk = line("junk")
    assert junk[0] == "nil" and "not a JPEG, PNG or WebP" in junk[1], junk
    noid = line("noid")
    assert noid[0] == "nil" and "no such request" in noid[1], noid
    assert line("sspr") == ["true"], line("sspr")

    assert line("fw1") == ["true", "bm/web/hello.txt"], line("fw1")
    assert line("fw2") == ["true", "bm/web/model.glb"], line("fw2")
    assert line("fw3") == ["true", "bm/web/hello.txt"], line("fw3")
    refused = [l.split("\t")[1:] for l in text.split("\n") if l.startswith("fwno\t")]
    assert len(refused) == 12, refused
    for p, r, e in refused:
        assert r == "nil" and e not in ("nil", ""), (p, r, e)
    assert "no such request" in line("fwid")[1], line("fwid")
    big = line("fwbig")
    assert big[0] == "nil" and "over the cap" in big[1], big
    fmax = line("fwmax")
    assert fmax[0] == "126" and "128 files" in fmax[1], fmax      # 126 + hello.txt + model.glb = 128
    assert "only https" in line("wg1")[1], line("wg1")
    assert "nil" == line("wg2")[0] and "space" in line("wg2")[1], line("wg2")
    assert "nil" == line("wg3")[0] and "fragment" in line("wg3")[1], line("wg3")
    assert line("wg4") == ["nil", "no network"], line("wg4")
    assert line("wp")[0] == "nil" and "no such request" in line("wp")[1], line("wp")
    assert line("wi")[0] == "nil" and line("wb")[0] == "nil", (line("wi"), line("wb"))
    assert line("wc") == ["false"], line("wc")

    try:
        part = os.path.join(tmp, "part.img")
        with open(img, "rb") as f, open(part, "wb") as o:
            f.seek(2048 * 512)
            o.write(f.read())
        fsck = subprocess.run(["fsck.vfat", "-n", part], capture_output=True, text=True)
        assert fsck.returncode == 0, fsck.stdout + fsck.stderr
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
        names = subprocess.run(["mdir", "-b", "-i", part, "::/BM/WEB"], capture_output=True, text=True,
                               env=env).stdout.upper().split()
        assert len(names) == 128, len(names)
        assert "::/BM/WEB/HELLO.TXT" in names and "::/BM/WEB/MODEL.GLB" in names, names[:5]
        hello = subprocess.run(["mtype", "-i", part, "::/BM/WEB/HELLO.TXT"], capture_output=True, env=env).stdout
        assert hello == b"hi again", hello
        glb = subprocess.run(["mtype", "-i", part, "::/BM/WEB/MODEL.GLB"], capture_output=True, env=env).stdout
        assert glb == b"x" * 100000, len(glb)
        root = subprocess.run(["mdir", "-b", "-i", part, "::/"], capture_output=True, text=True, env=env).stdout.upper()
        assert "KERNEL.IMG" not in root and "X.TXT" not in root and "BIG.BIN" not in root, root
        bm_dir = subprocess.run(["mdir", "-b", "-i", part, "::/BM"], capture_output=True, text=True, env=env).stdout.upper()
        assert "A.TXT" not in bm_dir and "SUB" not in bm_dir, bm_dir
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


BMWEB_DRIVER = """
-- test driver (tests/qemu_test.py): the joined cart's locals b (browser) and v (view) are in scope here
local __n, __done = 0, false
local __update = _update
function _update()
  __update()
  __n = __n + 1
  if __n == 5 then
    log("bmweb-test home " .. b.mode)
    b:go("https://example.test/a")
  end
  if __n > 5 and not __done and b.mode ~= "loading" then
    __done = true
    log("bmweb-test mode " .. b.mode .. " status " .. b.status .. " url " .. b.url)
    b:go("javascript:alert(1)")
    log("bmweb-test refused " .. b.mode)
    log("bmweb-test end")
  end
end
"""


def test_bmweb(b, opts):
    """bm Web (carts/bmweb) starts on the Pi kernel, draws its home page and, with no network (the QEMU
    machine has none), shows an error page for a request and for a refused address. The browser logic
    against a mock kernel is tested on the PC by make test-web."""
    import importlib.util
    spec = importlib.util.spec_from_file_location("bmweb_build", os.path.join(HERE, "..", "carts", "bmweb", "build", "build.py"))
    bb = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bb)
    src = bb.build() + BMWEB_DRIVER
    tmp = tempfile.mkdtemp(prefix="bm-bmweb-")
    img = os.path.join(tmp, "sd.img")
    cfg = os.path.join(tmp, "config.txt")
    with open(cfg, "w") as f:
        f.write("layout=us\nwifi_boot=0\n")
    mksd.build(img, [(cfg, "bm/config.txt")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    try:
        q.boot()
        assert _upload(q, mkbm.pack(src.encode(), title="bmweb"))
        out = q.expect("bmweb-test end", timeout=120)
        text = out.decode(errors="replace").replace("\r", "")
    finally:
        q.close()
        shutil.rmtree(tmp, ignore_errors=True)
    assert "bmweb: ready" in text, text[-2000:]
    assert "bmweb-test home home" in text, text[-2000:]
    mode = [l for l in text.split("\n") if "bmweb-test mode" in l]
    assert mode and "mode error" in mode[-1] and "Cannot load" in mode[-1], (mode, text[-2000:])
    assert "bmweb-test refused error" in text, text[-2000:]
    assert "attempt to" not in text and "stack traceback" not in text, text[-2000:]


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
    mksd.build(img, [(hcd, "bm/BCM43430A1.hcd"), (b("carts/snake.bm"), "bm/snake.bm")])
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
            # its first seconds are in the log: the light sent, the reports
            _mini_expect(q, "-> output report 11 (light 00 20 80")
            _mini_expect(q, "<- first input report 01")
            time.sleep(0.2)
            chip.report(0x08)
            _mini_expect(q, "playing snake.bm")
            # the pad drops: the log says why (the report of the log is all
            # the user has to go on)
            chip._event(0x05, bytes([0]) + chip.HANDLE.to_bytes(2, "little") + bytes([0x08]))
            _mini_expect(q, "bt: controller 1c:66:6d:01:02:03 (player 1) disconnected after ")
            _mini_expect(q, "input reports (last ")
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
    mksd.build(img, [(hcd, "bm/BCM43430A1.hcd"), (b("carts/pong.bm"), "bm/pong.bm"),
                     (b("carts/snake.bm"), "bm/snake.bm")])
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
        q.mini.write(b"4")
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
    mksd.build(img, [(hcd, "bm/BCM43430A1.hcd"), (b("carts/pong.bm"), "bm/pong.bm"),
                     (b("carts/snake.bm"), "bm/snake.bm")])
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


def test_pad_no_pointer(b, opts):
    """Only a mouse moves the pointer (the user, 2026-10-05; M32 had the
    right stick and R2 / L2 move and click it): with a DS4 paired, the
    right stick and the triggers show no arrow in the menu, and in a
    cartridge that asks for the pointer mouse() stays nil and nothing
    clicks."""
    tmp = tempfile.mkdtemp(prefix="bm-padptr-")
    img = os.path.join(tmp, "sd.img")
    hcd = os.path.join(tmp, "BCM43430A1.hcd")
    with open(hcd, "wb") as f:
        f.write(bytes([0x4C, 0xFC, 4, 1, 2, 3, 4, 0x4E, 0xFC, 4, 0xFF, 0xFF, 0xFF, 0xFF]))
    cart = os.path.join(tmp, "mouse.bm")
    with open(cart, "wb") as f:
        f.write(mkbm.pack(MOUSE_CART.encode(), title="Mouse test", res=(320, 180)))
    mksd.build(img, [(hcd, "bm/BCM43430A1.hcd"), (cart, "bm/mouse.bm")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"], mini_uart=True)
    q.mini_buf = b""

    def no_arrow(shot_):
        return not any(arrow_at(shot_, x, 180) for x in range(300, 636))

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
        assert no_arrow(wait_icons(q, 1)), "an arrow with only a pad"
        chip.report(rx=255)                             # the right stick, right
        time.sleep(0.3)
        chip.report(shoulders=0x08)                     # R2, then L2
        time.sleep(0.1)
        chip.report(shoulders=0x04)
        time.sleep(0.1)
        chip.report()
        time.sleep(0.3)
        assert no_arrow(q.screendump()), "the stick or the triggers showed the arrow"
        chip.report(0x08 | 0x20)                        # cross: plays the cover
        time.sleep(0.1)
        chip.report()
        _mini_expect(q, "playing mouse.bm")
        _mini_expect(q, "mouse nil")                    # asked for, nothing moves it
        chip.report(rx=0)
        time.sleep(0.3)
        chip.report(shoulders=0x08)
        time.sleep(0.1)
        chip.report(shoulders=0x04)
        time.sleep(0.1)
        chip.report()
        time.sleep(0.5)
        out = (q.mini_buf + q.mini.read(0.3)).decode(errors="replace")
        assert "mouse " not in out and " click" not in out, out
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
    mksd.build(img, [(hcd, "bm/BCM43430A1.hcd"), (cart, "bm/players.bm")])
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
    """The tabs with a DS4 (M27): R1 and L1 move between Market, Games, Dev
    and Settings (the menu opens on Games); on Settings its panel opens by itself;
    B out of it goes back to Dev. Up on the first row
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
                     (b("carts/astrowing.bm"), "bm/game.bm")])
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
        state(["Games"], ["Astro Wing"])

        press(shoulders=2)                  # R1: Dev
        state(["Dev"], ["bm SDK"])
        press(shoulders=2)                  # R1: Settings, its panel open
        state(["Settings"], ["Controllers", "WiFi and network"])
        if opts.shots:
            _save_png(q.screendump(), os.path.join(opts.shots, "home-tabs-settings.png"))

        # Settings > Controllers > Button icons: the DS4's face buttons of the
        # hints in their colours (the circle of Back red), then white again
        def red_hint(want=True):
            # the hints' row for up to a second: a screendump can fall while the
            # row is drawn again (CI, 2026-10-06: once no circle after the change)
            for _ in range(10):
                img_ = q.screendump()
                red = any(r > 220 and g < 140 and b_ < 140 for x in range(640)
                          for y in range(21 * 16, 22 * 16) for r, g, b_ in [pixel(img_, x, y)])
                if red == want:
                    break
                time.sleep(0.1)
            return red
        press(buttons=0x08 | 0x20)          # A (cross): Controllers
        state(["Settings"], ["Settings > Controllers"])
        press(buttons=0x00)                 # up three times: round to Button icons
        press(buttons=0x00)
        press(buttons=0x00)
        state(["Settings"], ["Button icons", "White"])
        assert not red_hint(False), "white button icons drawn red"
        press(buttons=0x08 | 0x20)          # A: Colour
        state(["Settings"], ["Colour"])
        assert red_hint(), "no red circle for Back"
        if opts.shots:
            _save_png(q.screendump(), os.path.join(opts.shots, "home-button-icons.png"))
        press(buttons=0x08 | 0x20)          # A: White
        state(["Settings"], ["White"])
        assert not red_hint(False), "the circle stayed red"
        press(buttons=0x08 | 0x40)          # B: back to Settings
        state(["Settings"], ["Controllers", "WiFi and network"], gone=["Button icons"])
        press(shoulders=2)                  # R1 on the last tab: nothing
        state(["Settings"], ["Controllers"])
        press(buttons=0x08 | 0x40)          # B (circle): out of Settings, back to Dev
        state(["Dev"], ["bm SDK"], gone=["Controllers"])
        press(shoulders=1)                  # L1: Games
        state(["Games"], ["Astro Wing"])
        press(shoulders=1)                  # L1: the Market, first (M25): QEMU has no network
        state(["Market"], ["No network: connect in Settings"])
        press(shoulders=1)                  # L1 on the first tab: nothing
        state(["Market"], ["No network: connect in Settings"])
        press(shoulders=2)                  # R1: back to Games
        state(["Games"], ["Astro Wing"])

        # up on the first row stays on the covers: R1 still goes to Dev
        chip.report(0x00)                   # hat up
        time.sleep(0.15)
        chip.report(0x08)
        time.sleep(0.4)
        state(["Games"], ["Astro Wing"])

        # PS in the menu: home, not the monitor
        press(shoulders=2)
        press(shoulders=2)
        press(shoulders=2)
        state(["Settings"], ["Controllers"])
        press(ps=1)
        state(["Games"], ["Astro Wing"], gone=["Controllers"])
        time.sleep(0.5)
        assert b"back to the monitor" not in q.mini_buf + q.mini.read(0.2), "PS left the menu"

        # PS in the monitor: the games menu
        q.mini.write(b"q")
        _mini_expect(q, "back to the monitor")
        _mini_expect(q, "> ")
        press(ps=1)
        _mini_expect(q, "cartridge menu")
        time.sleep(0.5)
        state(["Games"], ["Astro Wing"])
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


B16_GAME = r"""
function _init() log("the b16 runs") end
function _draw() cls(1) print("pocket", 8, 8, 7) end
"""


def test_b16_on_pi(b, opts):
    """An old .b16 (the same container) is listed on the Pi with the .bm and plays the same way (the user,
    2026-10-05)"""
    tmp = tempfile.mkdtemp(prefix="bm-b16-")
    try:
        cfg = os.path.join(tmp, "config.txt")
        with open(cfg, "w") as f:
            f.write("layout=us\nwifi_boot=0\n")
        cart = os.path.join(tmp, "pocket.b16")
        with open(cart, "wb") as f:
            f.write(mkbm.pack(B16_GAME.encode(), title="Pocket", author="tests"))
        img = os.path.join(tmp, "sd.img")
        mksd.build(img, [(cfg, "bm/config.txt"), (cart, "bm/pocket.b16")])
        q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
        try:
            q.expect(MENU, timeout=30)
            time.sleep(0.5)
            q.send("q")                                 # the monitor: the list of cartridges
            q.expect("> ", timeout=10)
            q.send("F")
            out = q.expect("pocket.b16", timeout=10).decode(errors="replace")
            out += q.expect("\n", timeout=5).decode(errors="replace")
            assert re.search(r"b16 +\d+ +/bm/pocket.b16", out), out[-300:]
            q.send("M")
            q.expect(MENU, timeout=20)
            time.sleep(0.8)
            q.send("\r")                                # the first game: the only one
            q.expect("the b16 runs", timeout=30)
        finally:
            q.close()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def test_grey_cart_on_pi(b, opts):
    """A cartridge whose header lists only the RGB30 is not hidden on the Pi (docs/spec/compat-policy.toml): the
    cover is faded (no pill), the start asks first (the reason is the question, OK and Cancel), back cancels, the
    confirm key starts it. An old .b16 starts with no question (test_b16_on_pi)."""
    tmp = tempfile.mkdtemp(prefix="bm-grey-")
    try:
        cfg = os.path.join(tmp, "config.txt")
        with open(cfg, "w") as f:
            f.write("layout=us\nwifi_boot=0\n")
        cart = os.path.join(tmp, "rgbonly.bm")
        with open(cart, "wb") as f:
            f.write(mkbm.mark_compat(mkbm.pack(B16_GAME.encode(), title="RGB30 only", author="tests"), 2))
        img = os.path.join(tmp, "sd.img")
        mksd.build(img, [(cfg, "bm/config.txt"), (cart, "bm/rgbonly.bm")])
        q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])

        def screen_has(*words):
            _, text = settled_screen(q, lambda i, t: all(any(w in l for l in t) for w in words))
            joined = "\n".join(text)
            for w in words:
                assert w in joined, f"{w!r} not on the screen:\n{joined}"
            return joined

        try:
            q.expect(MENU, timeout=30)
            time.sleep(0.8)
            screen_has("RGB30 only")
            assert "Not here" not in "\n".join(settled_screen(q, lambda i, t: True)[1]), "no pill on the cover"
            q.send("\r")                                # the first game: the question, it does not start
            text = screen_has("This edition is for the RGB30", "OK")
            time.sleep(1.0)
            assert b"the b16 runs" not in q.buf, "it started before the answer"
            q.send("q")                                 # no
            _, text = settled_screen(q, lambda i, t: not any("This edition is for the RGB30" in l for l in t))
            assert "This edition is for the RGB30" not in "\n".join(text), text
            time.sleep(0.5)
            assert b"the b16 runs" not in q.buf, "back must not start it"
            q.send("\r")
            screen_has("This edition is for the RGB30")
            q.send("\r")                                # OK: it starts (a faded cover is not a lock)
            q.expect("made for the RGB30 only, it may not run well here", timeout=30)   # the load-time message
            q.expect("the b16 runs", timeout=30)
        finally:
            q.close()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def test_market(b, opts):
    """M25: the Market tab, first in the menu, with a catalog in a folder of
    the SD card (slowed down by market_delay, as a network would be): the
    tab shows placeholders at once, the catalog and the covers arrive while
    the menu runs, nothing loads while another tab is shown, a game
    downloads with its progress on the cover, is checked, installed in
    /carts and plays; a game already on the card shows as installed; a file
    that is not the catalog's is refused; leaving the tab does not stop a
    download (dbf1c1c: it goes on in the background)."""
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
        mksd.build(img, [(cfg, "bm/config.txt"), (pub, "bm/market.pem"), (b("carts/pong.bm"), "bm/pong.bm")]
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
            screen([], gone=["Installed"])          # Pong: on the card already, with no pill
            shot("covers")

            # A on Market Test: a question, then the download with its progress
            screen(["Market Test", "Get", "Details"])
            keys("\r")
            screen(["Download Market Test?", "free, license MIT", "Download", "Cancel"])
            shot("ask")
            keys("\r")
            screen(["%"])                            # the badge with the percent
            shot("download")
            q.expect("market: games/mtest/mtest.bm -> /bm/mtest.bm", timeout=20)
            q.expect("market: Market Test installed", timeout=5)
            screen(["Play"], gone=["Installed"])
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

            # Snake: leaving the tab does not stop the download (dbf1c1c:
            # a game download goes on in the background)
            keys("a")
            screen(["Snake", "Get"])
            keys("\r\r")
            q.expect("market: downloading games/snake/snake.bm", timeout=10)
            keys("2")
            q.expect("market: Snake installed", timeout=20)
            screen(["Market Test"])                        # the Games tab shows the new games
        finally:
            q.close()

        part = os.path.join(tmp, "part.img")
        with open(img, "rb") as f, open(part, "wb") as o:
            f.seek(2048 * 512)
            o.write(f.read())
        fsck = subprocess.run(["fsck.vfat", "-n", part], capture_output=True, text=True)
        assert fsck.returncode == 0, fsck.stdout + fsck.stderr
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
        carts = subprocess.run(["mdir", "-b", "-i", part, "::/BM"], capture_output=True, text=True,
                               env=env).stdout.upper()
        assert "MTEST.BM" in carts and "SNAKE.BM" in carts and "BROKEN" not in carts, carts
        cache = subprocess.run(["mdir", "-b", "-i", part, "::/BM/MARKET"], capture_output=True, text=True,
                               env=env).stdout.upper()
        assert "INDEX.TXT" in cache and "INDEX.SIG" in cache and "GAMES.TXT" in cache and ".PNG" in cache, cache
        owned = subprocess.run(["mtype", "-i", part, "::/BM/MARKET/GAMES.TXT"], capture_output=True, text=True,
                               env=env).stdout
        lines = owned.splitlines()      # the two installed games, each with its hash and path
        assert len(lines) == 2 and lines[0].startswith("mtest ") and lines[0].endswith(" /bm/mtest.bm") \
            and lines[1].startswith("snake ") and lines[1].endswith(" /bm/snake.bm"), owned
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def test_update(b, opts):
    """M19 step 4: Settings > Updates > Check for updates reads a release
    signed with a test key (here from the SD card: update_url=sd:/release/,
    bm/release.pem), says which files would change (a game the same, one
    changed, one not on the card, the certificates, both kernels); Install
    asks first, downloads and checks everything, keeps the old kernel in
    /bm/backup, writes, and the console restarts. Both run in the menu's
    background (2026-10-11): the panel shows the check's result and the
    files, a box over the menu the restart counted down. Then the card holds
    the release's files, is a clean FAT32 volume, and the game deleted
    before is still not there."""
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
            keys("4")                               # Settings, then Updates
            screen(["Settings", "Controllers", "Updates"])
            keys("sssss")                             # Controllers, Controls, WiFi, Screen, Sound, Updates
            keys("\r")
            screen(["Settings > Updates", "Version"])
            keys("s")
            screen(["Check for updates", "not checked", "The latest release on GitHub"])
            keys("\r")
            out = q.expect("v9.9.9 can be installed", timeout=30).decode(errors="replace")
            for w in ("latest release: \x1b[1mv9.9.9", "commit abc1234, signed: good"):
                assert w in out, out
            # a build of the sources, or later than a tag (git describe): either may update
            assert "a build of the sources" in out or "newer than this kernel" in out, out
            lines = {l.split()[0]: l for l in out.replace("\r", "").splitlines() if l.startswith("  /")}
            assert lines["/kernel.img"].endswith("changed") and lines["/kernel7.img"].endswith("new"), lines
            assert lines["/carts/snake.bm"].endswith("changed") and lines["/carts/pong.bm"].endswith("same"), lines
            assert lines["/carts/shooter.bm"].endswith("not on the card (Market)"), lines
            assert lines["/bm/ca.pem"].endswith("changed"), lines
            # the menu stayed on the panel: the check's result, then what it would write
            screen(["Check for updates", "v9.9.9: 4 files", "/kernel.img", "changed", "/kernel7.img", "new"])
            keys("s")                               # the row under it: install
            screen(["Install the update", "v9.9.9", "Keeps the old kernels in /bm/backup"])
            keys("\r")
            screen(["Install bm v9.9.9?", "The console restarts when it is done.", "Install"])
            if opts.shots:
                _save_png(q.screendump(), os.path.join(opts.shots, "update-ask.png"))
            keys("\r")
            screen(["Restarting in", "bm v9.9.9 is installed"], tries=40)    # the box over the menu
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
    in the menu's background (2026-10-11), the panel staying: its rows say
    how it went (here without a network: not sent, and why). Then "Send to
    a nearby console" (M24): without a network, nobody to send to."""
    tmp = tempfile.mkdtemp(prefix="bm-publish-")
    try:
        img = os.path.join(tmp, "sd.img")
        cfg = os.path.join(tmp, "config.txt")
        with open(cfg, "w") as f:
            f.write("layout=us\nwifi_boot=0\ngithub_token=github_pat_test\n")
        cart = os.path.join(tmp, "game.bm")
        with open(cart, "wb") as f:
            f.write(mkbm.pack(MARKET_GAME.encode(), title="My Game", author="tests"))
        mksd.build(img, [(cfg, "bm/config.txt"), (cart, "bm/My Game.bm")])
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
            text = screen(["My Game", "Play", "Send to a nearby console"])
            for gone_row in ("Open in", "Make an editable copy", "Publish to the Market"):
                assert gone_row not in text, text

            # M24: send to a nearby console (here no network: nobody)
            keys("s")                               # Play, then Send
            screen(["Send to a nearby console", "To a console on this network with the Market tab open"])
            keys("\r")
            screen(["Send > My Game", "This console", "no network", "No console nearby yet"])
            keys("s")
            screen(["Connect in Settings > WiFi and network"])
        finally:
            q.close()
    finally:
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
                     (cart, "bm/players.bm")])
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
    """4 GB card (SDHC addressing); with a USB keyboard boot ends in the menu."""
    tmp = tempfile.mkdtemp(prefix="bm-sd-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [(b("carts/astrowing.bm"), "bm/game.bm")], 4096)
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"] + USB_KBD)
    try:
        out = q.expect("cartridge menu", timeout=90).decode(errors="replace")
        assert "sd: SDHC card (sdhost), FAT32, 4095 MB, label BMSD; 1 cartridges" in out, out
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
        opts_row = "Author"
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
        shutil.rmtree(tmp, ignore_errors=True)


def test_menu_scroll(b, opts):
    """Three rows of covers and two on screen: the scroll bar at the right
    says where they are, so the row scrolled out at the top is not taken for
    gone (15 cartridges: three rows of six)."""
    tmp = tempfile.mkdtemp(prefix="bm-scroll-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [(b("carts/astrowing.bm"), f"bm/g{i}.bm") for i in range(1, 16)])
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


MOUSE_CARTS = ["astrowing", "nano8", "pong", "snake", "shooter", "kitchen", "titan"]
# their titles, in the order of the menu
MOUSE_TITLES = ["Astro Wing", "Chaos Kitchen", "nano8", "Pong", "Snake", "Star Shooter", "Titan Clash"]


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
    mksd.build(img, [(b(f"carts/{n}.bm"), f"bm/{n}.bm") for n in MOUSE_CARTS])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}",
                               "-device", "usb-hub,port=1", "-device", "usb-kbd,port=1.2",
                               "-device", "usb-tablet,port=1.3"])
    def wait_screen(q, cond, timeout=15.0):             # a slow runner: 15 s, not 6
        return globals()["wait_screen"](q, cond, timeout)
    try:
        out = q.expect("cartridge menu", timeout=90).decode(errors="replace")
        assert "usb: port 3: if0 class 03/00/00 ep 81 mps 8" in out and " tablet" in out, out
        assert "usb: tablet (mouse) 0627:0001 'QEMU USB Tablet' (hub port 3), wheel" in out, out
        assert "usb: keyboard 0627:0001 'QEMU USB Keyboard'" in out, out
        # its icon once it has moved (2026-10-04: a keyboard's dongle declares
        # a mouse with none behind it)
        wait_menu(q)                                        # the first frame, later on a busy runner
        img_, _ = settled_screen(q, lambda i, t: len(bar_icons(i)) == 1, tries=20)
        assert len(bar_icons(img_)) == 1, "a mouse icon before the mouse did anything"
        q.pointer(320, 180)
        shot_ = wait_icons(q, 2, timeout=20)
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
        assert MOUSE_TITLES[6] in screen_text(wait_screen(q, title_is(MOUSE_TITLES[6])))[4]
        # the keys move the selection: the arrow goes away until it moves
        # press and release in one QMP call (sendkey holds the key 100 ms): the
        # key is up before the 500 ms repeat (hid.c), so it moves one cover
        q.input_events([{"type": "key", "data": {"down": d_, "key": {"type": "qcode", "data": "left"}}}
                        for d_ in (True, False)])
        moved = lambda s_: title_is(MOUSE_TITLES[5])(s_)
        shot_ = wait_screen(q, lambda s_: not arrow_at(s_, x, y) and moved(s_))
        assert not arrow_at(shot_, x, y), "the arrow stayed with the keys"
        assert MOUSE_TITLES[5] in screen_text(shot_)[4], screen_text(shot_)[4]

        def click_at(px, py, button="left"):        # once the arrow is there
            q.pointer(px, py)
            assert arrow_at(wait_screen(q, lambda s_: arrow_at(s_, px, py)), px, py), (px, py)
            q.click(button)
        # a click on Dev changes the tab, on Games back (Market | Games | Dev;
        # the Market off the screen at the left)
        click_at(132, 24)
        assert "bm SDK" in screen_text(wait_screen(q, title_is("bm SDK")))[4]
        click_at(68, 24)
        wait_screen(q, moved)                       # the selection it had
        # the right button on a cover: its options; a click outside closes them
        x, y = cover_xy(1)
        click_at(x, y, "right")
        text = "\n".join(screen_text(wait_screen(q, lambda s_: "Author" in "".join(screen_text(s_)))))
        assert "Play" in text and "Author" in text, text
        click_at(30, 200)
        # (the panel gone and the title back, in one wait: a frame between the two, as the
        # panel fades, has neither: it failed once under load)
        text = "\n".join(screen_text(wait_screen(
            q, lambda s_: "Author" not in "".join(screen_text(s_)) and
            MOUSE_TITLES[1] in "".join(screen_text(s_)))))
        assert "Author" not in text and MOUSE_TITLES[1] in text, text
        # a click on a cover plays it
        click_at(*cover_xy(0))
        q.expect("playing astrowing.bm", timeout=20)
        q.expect("bm: loaded", timeout=20)
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
    mksd.build(img, [(cart, "bm/mouse.bm")])
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
    mksd.build(img, [(square, "bm/square.bm")])
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
    mksd.build(img, [(cart, "bm/mouse.bm"), (os.path.join(tmp, "config.txt"), "bm/config.txt")])
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


def test_game_kit(b, opts):
    """The gameplay kit on the ARM: the cartridge of make test-gamekit (a
    small game on lib.world, lib.states, lib.input, lib.tween). Without the
    input script of bmhost it runs the kit's checks of _init, all of them
    pass, and the title state draws its text."""
    cart = b("gamekit-test.bm")
    if not os.path.exists(cart):
        subprocess.run(["make", "-s", cart], check=True, cwd=os.path.dirname(os.path.abspath(__file__)) + "/..")
    tmp = tempfile.mkdtemp(prefix="bm-gamekit-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    try:
        q.boot()
        assert _upload(q, open(cart, "rb").read())
        q.expect("received", timeout=30)
        out = q.expect("gamekit: kit", timeout=30).decode(errors="replace")
        out += q.expect(" ok", timeout=10).decode(errors="replace")
        time.sleep(1.0)
        m = re.search(r"gamekit: kit (\d+)/(\d+) ok", out)
        assert m and m.group(1) == m.group(2) and int(m.group(2)) >= 10, out[-3000:]
        assert "FAIL" not in out and "stopped with an error" not in out, out[-3000:]
        text = screen_text(q.screendump())
        assert any("game kit" in l for l in text), "\n".join(text)
    finally:
        q.close()
        shutil.rmtree(tmp, ignore_errors=True)


def test_pong_kit(b, opts):
    """Pong, moved onto the gameplay kit (lib.world for the paddles and the ball,
    lib.sounds for its notes and tunes): it starts on the ARM, runs without an
    error and draws its title. Its play is compared with the old version by
    make test-game-trace."""
    cart = b("carts/pong.bm")
    if not os.path.exists(cart):
        subprocess.run(["make", "-s", cart], check=True, cwd=os.path.dirname(os.path.abspath(__file__)) + "/..")
    tmp = tempfile.mkdtemp(prefix="bm-pongkit-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    try:
        q.boot()
        assert _upload(q, open(cart, "rb").read())
        q.expect("received", timeout=30)
        time.sleep(2.0)
        q.buf += q.port.read(0.5)
        out = q.buf.decode(errors="replace")
        assert "stopped with an error" not in out and "attempt to" not in out, out[-3000:]
        text = screen_text(q.screendump())
        assert any("P O N G" in l for l in text), "\n".join(text)
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
    the dev kit's performance overlay ('p' from the serial line, F11): at the
    top right, at the bottom right, off"""
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
        q.send("p")                             # the performance overlay: at the top right
        time.sleep(1.5)
        text = screen_text(box(q.screendump()))
        top = [i for i, l in enumerate(text) if re.search(r"\d+fps \d+\.\dms", l)]
        assert top == [0], "\n".join(text)
        assert re.search(r"cpu.*\d+%", text[1]), "\n".join(text)
        q.send("p")                             # at the bottom right: the last two rows of the box
        time.sleep(0.6)
        text = screen_text(box(q.screendump()))
        low = [i for i, l in enumerate(text) if re.search(r"\d+fps \d+\.\dms", l)]
        assert low == [len(text) - 2], "\n".join(text)
        assert re.search(r"cpu.*\d+%", text[-1]), "\n".join(text)
        if opts.shots:
            _save_png(q.screendump(), os.path.join(opts.shots, "square-perf-bottom.png"))
        q.send("p")                             # off
        time.sleep(0.6)
        text = screen_text(box(q.screendump()))
        assert not any(re.search(r"\d+fps \d+\.\dms", l) for l in text), "\n".join(text)
        q.send("q")
        out = q.expect("update+draw", timeout=10).decode(errors="replace")
        assert "stopped with an error" not in out, out
    finally:
        q.close()


def test_herocart(b, opts):
    """Hero Stress (carts/herocart, stage 1 of docs/design/hero-stress-cart-and-cel-shader.md) on the
    ARM raster of QEMU: it starts, prints its 1 s line for 4 seconds with no Lua error; R1 ('o' from the
    serial line) draws one hero more, X ('c') changes the polygon level, down ('s') and right ('d') change the row's value"""
    q = Qemu(b("kernel.img"))
    seen = ""
    try:
        q.expect(MENU, timeout=30)
        time.sleep(0.5)
        with open(b("carts/herocart.bm"), "rb") as f:
            assert _upload(q, f.read())

        def wait(needle, timeout=90):
            nonlocal seen
            seen += q.expect(needle, timeout=timeout).decode(errors="replace")

        def state():
            """The cart's last printed state: (frame, cursor row, the 1 s line)."""
            f = list(re.finditer(r"hkeys f=(\d+) row=(\d+)", seen))[-1]
            return int(f.group(1)), int(f.group(2)), seen[f.end():]

        def settle(timeout=240):
            """Returns after a 1 s line printed 11 frames or more after the second one that follows
            now (the first can be one that was printed before the key was sent). A serial key counts as held for 10 frames (HOLD_FRAMES in runtime.c); the same key
            sent inside that time makes no new press. QEMU runs the cart at 2 to 3 fps, so the
            time of that hold is seconds: the test counts frames, not time."""
            wait("kb=", timeout=timeout)
            wait("kb=", timeout=timeout)
            first = state()[0]
            while True:
                wait("kb=", timeout=timeout)
                if state()[0] >= first + 11:
                    return state()

        def key(k, reached, tries=6):
            """Sends k and waits until the printed state shows it was taken (reached(row, line));
            sends it again, after the hold, if not (a key can be lost while the machine is busy)."""
            for _ in range(tries):
                q.send(k)
                _, row, line = settle()
                if reached(row, line):
                    return
            raise AssertionError(f"key {k!r}: the state never came\n{seen[-1500:]}")

        wait(" n=20 lod=auto res=640x360 opt=")
        wait("kb=")
        wait("herocart 1 t=")          # frames go on: a second and another line
        wait("kb=")
        settle()
        key("o", lambda r, s: " n=21 lod=auto " in s)      # R1: a hero more
        key("c", lambda r, s: " n=21 lod=3 " in s)         # X: the level before auto: 3
        key("s", lambda r, s: r == 2)                      # down to the LOD row
        key("d", lambda r, s: " n=21 lod=auto " in s)      # right: the next value, auto
        key("w", lambda r, s: r == 1)                      # up twice: the first row, then the last (cel bands)
        key("w", lambda r, s: r == 20)
        key("d", lambda r, s: ",cel2," in s)               # right: 2 bands (cel light on the ARM raster)
        key("w", lambda r, s: r == 19)                     # up twice: the Resolution row
        key("w", lambda r, s: r == 18)
        for res in ("960x540", "1280x720", "1920x1080"):   # right three times: 1920x1080
            key("d", lambda r, s, res=res: f" res={res} " in s)
        q.send("q")
        q.expect("update+draw", timeout=10)
        assert "stopped with an error" not in seen, seen[-1500:]
        assert "kb=" in seen
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


def test_monitor_line(b, opts):
    """The monitor's ':' line (2026-10-05): commands one after the other,
    ';' between them; set changes a key until a restart, b3d runs a part of
    the 3D Bench without waiting on its pages, a word not known stops the
    line and says so, err reports runtime errors. Typed in the menu (as
    bm_net.py --line does it, or a paste in easy_install's monitor), the ':'
    takes the line to the monitor."""
    tmp = tempfile.mkdtemp(prefix="bm-line-")
    img = os.path.join(tmp, "sd.img")
    cfg = os.path.join(tmp, "config.txt")
    bad = os.path.join(tmp, "bad.bm")
    with open(cfg, "w") as f:
        f.write("layout=us\nwifi_boot=0\n")
    with open(bad, "wb") as f:
        f.write(mkbm.pack(b"function _update() boom() end\nfunction _draw() end", title="bad"))
    mksd.build(img, [(cfg, "bm/config.txt"), (bad, "bm/bad.bm")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    try:
        q.expect(MENU, timeout=30)
        wait_menu(q)                            # a ':' typed before the menu runs is lost
        q.send(":err\r")
        out = q.expect("the line is done", timeout=10).decode(errors="replace")
        assert "err: none" in out, out
        assert "running: menu" in out, out
        q.expect("> ")
        q.send(":set gpu3d_fs2=1 gpu3d_sort=1; b3d tests=spheres,quad_flat profiles=ARM; nope; send\r")
        q.expect("set gpu3d_fs2=1 (until a restart", timeout=10)
        q.expect("set gpu3d_sort=1", timeout=10)
        out = q.expect("3D Bench: done", timeout=240).decode(errors="replace")
        assert "only tests spheres,quad_flat, profiles ARM" in out, out[-2000:]
        assert "R,spheres,ARM" in out and "R,quad_flat,ARM" in out, out[-2000:]
        assert "R,heroes," not in out and "R,match," not in out, out[-2000:]
        out = q.expect("the line is done", timeout=30).decode(errors="replace")
        assert "'nope': not a command of the line" in out, out
        assert ": send" not in out, out             # the line stopped at the unknown word
        q.expect("> ")
        # the fade on the QPU profile: no gpu3d_qpu key here, so the ARM runs it and the report says so
        q.send(":b3d tests=fade profiles=ARM,QPU; send\r")
        out = q.expect("3D Bench: done", timeout=240).decode(errors="replace")
        assert "R,fade,ARM" in out and "R,fade,QPU" in out, out[-2000:]
        assert "qpufade key=0 path=arm" in out, out[-2000:]
        q.expect("the line is done", timeout=30)
        q.expect("> ")
        # the stress test needs the V3D (GPU+VS+Q): QEMU has none, so the line runs and the report has no stress rows
        q.send(":b3d tests=stress; send\r")
        out = q.expect("3D Bench: done", timeout=240).decode(errors="replace")
        assert "only tests stress, profiles all" in out, out[-2000:]
        assert "R,stress," not in out and "stress path=" not in out, out[-2000:]
        q.expect("the line is done", timeout=30)
        q.expect("> ")
        q.send(":play bad; err\r")
        out = q.expect("the line is done", timeout=30).decode(errors="replace")
        assert "attempt to call a nil value" in out, out
        q.expect("> ")
        q.send("l")                             # the key is set for now
        q.expect("lua> ")
    finally:
        q.close()
        shutil.rmtree(tmp, ignore_errors=True)


def test_monitor_crc(b, opts):
    """The monitor's ':crc' (R-NET7): the CRC32 of the page on screen, in the
    line's fixed format; the same screen gives the same value, another screen
    another one."""
    tmp = tempfile.mkdtemp(prefix="bm-crc-")
    img = os.path.join(tmp, "sd.img")
    cfg = os.path.join(tmp, "config.txt")
    with open(cfg, "w") as f:
        f.write("layout=us\nwifi_boot=0\n")
    mksd.build(img, [(cfg, "bm/config.txt")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    try:
        q.expect(MENU, timeout=30)
        time.sleep(0.5)

        def crc():
            q.send(":crc\r")
            out = q.expect("the line is done", timeout=10).decode(errors="replace")
            m = re.search(r"crc: (\d+)x(\d+) bpp=(\d+) pitch=(\d+) crc32=0x([0-9a-f]{8})\b", out)
            assert m, out
            q.expect("> ")
            return m

        m = crc()
        w, h, bpp, pitch = map(int, m.group(1, 2, 3, 4))
        assert w > 0 and h > 0 and bpp == 32 and pitch >= w * 4, m.group(0)
        first = m.group(5)
        assert first != "00000000", m.group(0)
        # console_clear() gives the same screen, but for the status line: it shows
        # the uptime in seconds (src/kernel/input.c update_uptime), so two clears on
        # either side of a second differ. Clear and ask until two in a row agree: they
        # do within one second, and a CRC that did not follow the screen never would.
        seen = []
        for _ in range(20):
            q.send("c")
            q.expect("> ")
            seen.append(crc().group(5))
            if len(seen) > 1 and seen[-1] == seen[-2]:
                break
        assert len(seen) > 1 and seen[-1] == seen[-2], f"the same screen, never the same value: {seen}"
        assert first != seen[-1], f"the screen with ':crc' printed and the cleared one give the same value {first}"
    finally:
        q.close()
        shutil.rmtree(tmp, ignore_errors=True)


def _plain(data):
    """serial output as text: no colour codes, no carriage returns"""
    return re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", data.decode(errors="replace")).replace("\r", "")


def test_monitor_help_table(b, opts):
    """The help is made from the table of commands (src/kernel/monitor_cmd.c): one
    line for each key, none longer than the console (79 characters: the pager wraps at 80), the old
    words in a last line; 'help NAME' says more about one command."""
    q = Qemu(b("kernel.img"))
    try:
        q.boot()
        for key in "h?":
            q.send(key)
            text = ""
            for _ in range(12):
                text += _plain(q.expect("-- lines ", timeout=5)).removesuffix("-- lines ") + "\n"
                span = _plain(q.expect("returns --", timeout=5))
                first, last, total = map(int, re.match(r"(\d+)-(\d+) of (\d+)", span).groups())
                if last >= total:
                    break
                q.send(" ")
            q.send("q")
            q.expect("> ", timeout=5)
            assert "commands (games:" in text and "help [COMMAND]" in text, text[-600:]
            keys = set(re.findall(r"^  (\S)  \S", text, re.M))
            want = set("hMfFlUimcoyYLVatXrTKOPWEGkpjdzZu3456eACI")
            assert want <= keys, f"keys missing in the help: {sorted(want - keys)}"
            assert "old words that still work: b3d=bench 3d," in text, text[-500:]
            assert all(len(l) <= 79 for l in text.splitlines()), [l for l in text.splitlines() if len(l) > 79]
            assert "wifi join NAME [PASSWORD]" in text and "bench 3d [tests=A,B profiles=P]" in text, text[-900:]
    finally:
        q.close()


def _prompt_line(q, text, timeout=10):
    """a line typed at the bm> prompt (bytes of text as they are, then Enter);
    what it printed, up to the next prompt"""
    q.send(text + "\r")
    return _plain(q.expect("\nbm> ", timeout=timeout))


def test_monitor_prompt(b, opts):
    """The persistent line mode (2026-10-25): ':' and Enter open the bm> prompt, which stays
    until 'exit' or Esc. Commands and subcommands, a suggestion for an unknown word, 'set'
    and 'crc', the history on Up (a terminal's ESC [ A, also one byte at a time), Tab
    completion of commands, subcommands and set keys, Backspace and Ctrl-U, '\\r\\n' as
    one Enter, Ctrl-C; the one-shot ':line' of the PC tools is as before."""
    q = Qemu(b("kernel.img"))
    try:
        q.boot()
        q.send(":\r")
        q.expect("the prompt stays", timeout=5)
        q.expect("\nbm> ", timeout=5)
        out = _prompt_line(q, "help wifi")
        assert "wifi join NAME [PASSWORD]" in out and "wifi forget" in out and "wifi list" in out, out
        out = _prompt_line(q, "help nosuch")
        assert "help: no command 'nosuch'" in out, out
        out = _prompt_line(q, "sned")
        assert "'sned': not a command of the line; did you mean: send" in out, out
        out = _prompt_line(q, "wifi nosuch")
        assert "'wifi': not a subcommand: 'nosuch'; use: list, join, forget" in out, out
        out = _prompt_line(q, "bt")
        assert "'bt': needs one of: pair, forget" in out, out
        out = _prompt_line(q, "set gpu3d_sort=1 name=\"a b\"")
        assert "set gpu3d_sort=1 (until a restart" in out and "set name=a b (until" in out, out
        out = _prompt_line(q, "set")
        assert "gpu3d_sort=1" in out and "name=a b" in out, out
        out = _prompt_line(q, "set oops")
        assert "set: 'oops' is not key=value" in out, out
        out = _prompt_line(q, "crc")
        assert re.search(r"crc: (\d+)x(\d+) bpp=(\d+) pitch=(\d+) crc32=0x([0-9a-f]{8})\b", out), out
        out = _prompt_line(q, "err; net status")
        assert "err: none" in out and "net: wifi off" in out, out
        out = _prompt_line(q, "err; nope; net status")
        assert "'nope': not a command" in out and "net: wifi" not in out, out     # a bad word stops the line
        # history: Up brings back the last line, Up Up Down the one before... of the last
        out = _prompt_line(q, "\x1b[A")
        assert "'nope': not a command" in out, out
        out = _prompt_line(q, "\x1b[A\x1b[A\x1b[B")
        assert "'nope': not a command" in out, out
        # a terminal may send the arrow's bytes one at a time
        for ch in "\x1b[A":
            q.send(ch)
            time.sleep(0.01)
        q.expect("nope; net status", timeout=5)
        q.send("\x15")                                      # Ctrl-U
        q.send("crc\r")
        out = _plain(q.expect("\nbm> ", timeout=10))
        assert "crc: " in out and "net: wifi" not in out, out
        # Tab: a command, a subcommand, a key of set
        q.send("cr\t")
        q.expect("bm> crc ", timeout=5)
        q.send("\r")
        assert "crc: " in _plain(q.expect("\nbm> ", timeout=10))
        q.send("wi\tjo\t")
        q.expect("bm> wifi join ", timeout=5)
        q.send("\x15")
        q.send("set vol\t")
        q.expect("bm> set volume=", timeout=5)
        q.send("\x15")
        q.send("sy\tcr\t")
        q.expect("bm> sys crash ", timeout=5)
        q.send("\x15")
        # Tab with several words: the list, then the line again
        q.send("se\t")
        out = _plain(q.expect("bm> se", timeout=5))
        assert "send" in out and "set" in out and "sys" not in out, out
        q.send("\x03")                                      # Ctrl-C drops the line
        q.expect("^C", timeout=5)
        q.expect("\nbm> ", timeout=5)
        # Backspace, and \r\n as one Enter: no empty line behind it
        q.send("crx\x7fc\r\n")
        out = _plain(q.expect("\nbm> ", timeout=10))
        assert "crc: " in out, out
        q.send("err\r\n")
        out = _plain(q.expect("\nbm> ", timeout=10))
        assert "err: none" in out, out
        time.sleep(0.3)
        assert b"\nbm> " not in q.buf, q.buf                # no second prompt for the \n
        # the word of before inside the prompt
        out = _prompt_line(q, "b3d tests=spheres profiles=ARM", timeout=240)
        assert "only tests spheres, profiles ARM" in out, out[-600:]
        # exit: back to the key prompt; the keys work again; ':' with a line is one-shot
        q.send("exit\r")
        q.expect("\n> ", timeout=5)
        q.send("m")
        assert "heap" in _plain(q.expect("\n> ", timeout=5))
        q.send(":set name=\"q r\"; err\r")
        out = _plain(q.expect("the line is done", timeout=10))
        assert ": set name=q r" in out and "set name=q r (until" in out and "err: none" in out, out
        q.expect("\n> ", timeout=5)
        # Esc leaves the prompt too (a lone ESC byte: the Esc key of a keyboard)
        q.send(":\r")
        q.expect("\nbm> ", timeout=5)
        q.send("\x1b")
        q.expect("\n> ", timeout=5)
        # Ctrl-D on an empty line leaves
        q.send(":\r")
        q.expect("\nbm> ", timeout=5)
        q.send("\x04")
        q.expect("\n> ", timeout=5)
        # Esc in a one-shot ':' drops it
        q.send(":")
        q.send("\x1b")
        q.expect("\n> ", timeout=5)
        assert b"the line is done" not in q.buf
    finally:
        q.close()


def test_monitor_keys_and_words(b, opts):
    """Every single key still works as before, and the word of its command does the same
    (the keys are shortcuts of the table). Keys that need hardware or a long wait are
    not here (T K O P W G u X k p j d D t Y: other tests)."""
    q = Qemu(b("kernel.img"))
    try:
        q.boot()

        def key(k):
            q.send(k)
            return _plain(q.expect("\n> ", timeout=20))

        def words(w):
            q.send(":" + w + "\r")
            out = _plain(q.expect("the line is done", timeout=20))
            q.expect("\n> ", timeout=5)
            return out

        pairs = [("i", "sys info", "board revision"), ("m", "sys heap", "heap"),
                 ("f", "carts list", "bm SDK"), ("F", "carts rescan", "sd: no card"),
                 ("y", "sys usb", "usb: nothing attached"), ("E", "net eth", "eth: no Ethernet controller"),
                 ("a", "sys audio", "audio:"), ("z", "report send", "waiting on the SD card"),
                 ("Z", "report log", "report: "), ("L", "sys layout", "keyboard layout: "),
                 ("V", "sys via-ram", ".bm carts draw ")]
        for k, w, want in pairs:
            a, c = key(k), words(w)
            assert want in a, (k, a)
            assert want in c, (w, c)
        # the toggles went twice each (a key, a word): set them by name
        assert "keyboard layout: us" in words("sys layout us")
        assert "keyboard layout: it" in words("sys layout it")
        assert "directly on screen" in words("sys via-ram off")
        assert "via a RAM buffer" in words("sys via-ram on")
        assert "layout: it or us" in words("sys layout xx")
        # the old words and the commands that replace them
        for old, new, want in (("send", "report send", "waiting on the SD card"), ("log", "report log", "report: "),
                               ("err", "err", "err: none"), ("crc", "crc", "crc: ")):
            assert want in words(old) and want in words(new), old
        # c clears the screen, h and ? are the help, an unknown key is said
        key("c")
        q.send("h")
        q.expect("-- lines 1-", timeout=5)
        q.send("q")
        q.expect("> ", timeout=5)
        q.send("?")
        q.expect("-- lines 1-", timeout=5)
        q.send("q")
        q.expect("> ", timeout=5)
        assert "unknown command (0x78)" in key("x")
        # help of one command, and of an old word
        q.send(":help b3d\r")
        out = _plain(q.expect("\n> ", timeout=10))
        assert "b3d is the old word for 'bench 3d'" in out and "bench 3d [tests=A,B profiles=P]" in out, out
        out = words("sys crash nosuch")
        assert "crash: undef, dabt, pabt, svc, freeze or freeze-irq" in out, out
        # sys crash with a word: the same crash as X and a key
        q.send(":sys crash dabt\r")
        q.expect("Data abort", timeout=15)
    finally:
        q.close()


def test_reports(b, opts):
    """The reports (src/kernel/reports.c, 2026-10-04): Z in the monitor makes
    one of the log, saved as bm/reports/rpt00001.txt with its header (kernel,
    branch, board, date, the long name with kernel and branch), the token
    masked where the log had it (the repository may be public); with a token
    but no network (QEMU) it waits on the card, and z says so; p (the
    render bench) makes its own of what it printed."""
    tmp = tempfile.mkdtemp(prefix="bm-reports-")
    img = os.path.join(tmp, "sd.img")
    cfg = os.path.join(tmp, "config.txt")
    with open(cfg, "w") as f:
        f.write("layout=us\nwifi_boot=0\ngithub_token=test-token-4242\n")
    mksd.build(img, [(cfg, "bm/config.txt"), (b("carts/astrowing.bm"), "bm/game.bm")])
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
        m = re.search(r"report: (nodate-[0-9a-f]{6}_log_\S+_\S+\.txt) saved as bm/reports/rpt00001.txt", out)
        assert m, out
        name = m.group(1)
        q.expect("> ")
        q.send("z")
        out = q.expect("waiting on the SD card", timeout=20).decode(errors="replace")
        assert "on the SD card: no network; 1 waiting" in out, out
        q.expect("> ")
        q.send("p")                             # the render bench: a report of its printout
        out = q.expect("rpt00002.txt", timeout=120).decode(errors="replace")
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
    log = subprocess.run(["mtype", "-i", part, "::/BM/REPORTS/rpt00001.txt"], capture_output=True, text=True,
                         env=env).stdout
    render = subprocess.run(["mtype", "-i", part, "::/BM/REPORTS/rpt00002.txt"], capture_output=True, text=True,
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


def test_crash_report(b, opts):
    """A crash becomes a report (2026-10-06: on the Pi without a serial
    cable): the red screen shows who called (the return addresses on the
    stack), after 60 s the watchdog restarts the Pi, which keeps its memory,
    and the next boot saves the crash as a report with the lines printed
    before it (src/kernel/crumbs.c). Not in the first 20 s of a boot."""
    tmp = tempfile.mkdtemp(prefix="bm-crash-")
    img = os.path.join(tmp, "sd.img")
    cfg = os.path.join(tmp, "config.txt")
    with open(cfg, "w") as f:
        f.write("layout=us\nwifi_boot=0\n")
    mksd.build(img, [(cfg, "bm/config.txt"), (b("carts/astrowing.bm"), "bm/game.bm")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    try:
        q.expect(MENU, timeout=30)
        time.sleep(0.5)
        q.send("q")
        q.expect(PROMPT)
        q.expect("> ")
        time.sleep(21)                          # past the first 20 s of the boot
        q.send("X")
        q.expect("other keys cancel")
        q.send("a")
        out = q.expect("Restarting in 60 s", timeout=20).decode(errors="replace")
        assert re.search(r"called from:( [0-9a-f]{8})+", out), out
        q.expect("the last run ended in a crash: saved as a report", timeout=120)
        out = q.expect("rpt00001.txt", timeout=30).decode(errors="replace")
        assert re.search(r"report: nodate-[0-9a-f]{6}_crash_", out), out
        q.expect(MENU, timeout=30)
    finally:
        q.close()
    part = os.path.join(tmp, "part.img")
    with open(img, "rb") as f, open(part, "wb") as o:
        f.seek(2048 * 512)
        o.write(f.read())
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    rep = subprocess.run(["mtype", "-i", part, "::/BM/REPORTS/rpt00001.txt"], capture_output=True, text=True,
                         env=env).stdout
    shutil.rmtree(tmp, ignore_errors=True)
    head, body = rep.split("\n\n", 1)
    assert "\x00" not in rep, "null bytes in crash report"
    assert "kind: crash" in head.splitlines(), rep[:600]
    assert body.startswith('a crash (the red screen) while: monitor command "X"'), body[:300]
    assert ", kernel " in body.splitlines()[0], body.splitlines()[0]
    assert ", ring " in body.splitlines()[0] and "sink " in body.splitlines()[0], body.splitlines()[0]
    assert "--- the last lines printed ---" in body and "*** EXCEPTION: Data abort ***" in body, body[-1500:]
    assert "restart in 10 s" in body, body[-1500:]
    assert body.index("cartridge menu") < body.index("*** EXCEPTION"), "the lines before the crash"
    assert "DFAR=00008001" in body and "called from:" in body, body[-800:]


def test_menu_scale(b, opts):
    """menu_scale=3 in bm/config.txt (26 Oct 2026): the menu asks for 1080p, which only the GPU draws. QEMU has no
    V3D, so the menu stays at 360p on the ARM (640x360, no ARM enlarge) and says so in the log."""
    tmp = tempfile.mkdtemp(prefix="bm-scale-")
    img = os.path.join(tmp, "sd.img")
    cfg = os.path.join(tmp, "config.txt")
    with open(cfg, "w") as f:
        f.write("layout=us\nmenu_scale=3\n")
    mksd.build(img, [(cfg, "bm/config.txt"), (b("carts/astrowing.bm"), "bm/game.bm")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    try:
        q.expect(MENU, timeout=30)
        q.expect("the GPU cannot draw 3x, 360p on the ARM", timeout=10)
        for _ in range(30):
            w, h, px = q.screendump()
            text = screen_text((w, h, px))
            if "Settings" in text[1] and "Astro Wing" in text[4]:
                break
            time.sleep(0.3)
        assert (w, h) == (640, 360), f"screen {w}x{h}"
        assert "Games" in text[1] and "Settings" in text[1] and "Astro Wing" in text[4], "\n".join(text)
        if opts.shots:
            _save_png((w, h, px), os.path.join(opts.shots, "menu-360-fallback.png"))
    finally:
        q.close()
        shutil.rmtree(tmp, ignore_errors=True)


def test_menu_scale_360_gpu(b, opts):
    """menu_scale=4 (360p drawn by the GPU): QEMU has no V3D, so the ARM draws the 640x360 menu and the log says so."""
    tmp = tempfile.mkdtemp(prefix="bm-scale4-")
    img = os.path.join(tmp, "sd.img")
    cfg = os.path.join(tmp, "config.txt")
    with open(cfg, "w") as f:
        f.write("layout=us\nmenu_scale=4\n")
    mksd.build(img, [(cfg, "bm/config.txt"), (b("carts/astrowing.bm"), "bm/game.bm")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    try:
        q.expect(MENU, timeout=30)
        q.expect("the GPU cannot draw 360p, the ARM does", timeout=10)
        for _ in range(30):
            w, h, px = q.screendump()
            text = screen_text((w, h, px))
            if "Settings" in text[1] and "Astro Wing" in text[4]:
                break
            time.sleep(0.3)
        assert (w, h) == (640, 360), f"screen {w}x{h}"
        assert "Games" in text[1] and "Settings" in text[1] and "Astro Wing" in text[4], "\n".join(text)
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
    log(string.format("frameskip draws %d updates %d in %.2f s, stat15 %d, presses %d held %d, stat16 %.1f",
                      draws - d0, ups - u0, time() - t0, stat(15), presses, held, stat(16)))
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
        m = re.search(r"frameskip draws (\d+) updates (\d+) in ([\d.]+) s, stat15 (\d+), presses (\d+) held (\d+), "
                      r"stat16 ([\d.]+)", out)
        assert m, out[-400:]
        draws, ups, secs, last, presses, held = (int(m.group(1)), int(m.group(2)), float(m.group(3)),
                                                 int(m.group(4)), int(m.group(5)), int(m.group(6)))
        print(f"     {draws} draws, {ups} updates in {secs:.2f} s (game time {ups / 60:.2f} s), "
              f"last frame {last}, A pressed {presses}, held {held} updates")
        assert ups >= 2 * draws, (ups, draws)
        assert 2 <= last <= 4, last
        assert abs(ups / 60 - secs) < 0.25 * secs, (ups, secs)
        assert presses == 1 and held > 10, (presses, held)
        assert float(m.group(7)) == 0, m.group(7)   # stat(16): no GPU in QEMU, no job time
        q.send("q")
        out = q.expect("update+draw", timeout=20).decode(errors="replace")
        assert "stopped with an error" not in out, out
    finally:
        q.close()


STATPERF_CART = r"""
local n = 0
function _init() log("statperf start") end
function _update() end
function _draw()
  n = n + 1
  cls(1)
  if n == 30 then
    local a, b, c = stat(16), stat(17), stat(18)
    log(string.format("statperf %s %s %s | %.3f %.3f %.3f", type(a), type(b), type(c), a, b, c))
  end
end
"""


def test_stat_perf(b, opts):
    """stat(16), stat(17), stat(18) (GPU job time, audio time, GPU wait of the
    last frame, in ms): numbers, not negative, below one second. A
    measurement only: no value is asserted exactly (QEMU has no GPU) and no
    cartridge may use them in game logic (a lockstep match falls apart)"""
    q = Qemu(b("kernel.img"))
    try:
        q.expect(MENU, timeout=30)
        time.sleep(0.5)
        assert _upload(q, mkbm.pack(STATPERF_CART.encode(), title="statperf"))
        q.expect("statperf start", timeout=30)
        out = q.expect("statperf number", timeout=30).decode(errors="replace")
        out += q.expect("\n", timeout=5).decode(errors="replace")
        m = re.search(r"statperf number number number \| ([\d.-]+) ([\d.-]+) ([\d.-]+)", out)
        assert m, out[-400:]
        for name, g in zip((16, 17, 18), m.groups()):
            v = float(g)
            print(f"     stat({name}) = {v:.3f} ms")
            assert 0 <= v < 1000, (name, v)
        q.send("q")
        out = q.expect("update+draw", timeout=20).decode(errors="replace")
        assert "stopped with an error" not in out, out
    finally:
        q.close()
    # not for logic: a cartridge that reads them stays away from netplay
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    for d, _, files in os.walk(os.path.join(root, "carts")):
        for f in files:
            if f.endswith(".lua"):
                with open(os.path.join(d, f), encoding="utf-8", errors="replace") as fh:
                    t = fh.read()
                if re.search(r"stat\(1[678]\)", t):
                    assert not re.search(r"net\.|lockstep|netplay|send_input", t), f"{f}: stat(16-18) in a netplay file"


DEVKIT_CART = r"""
function _init()
  log("devkit was " .. devkit(2) .. ", now " .. devkit())
end
function _update() end
function _draw() cls(1) end
"""


def test_devkit_per_run(b, opts):
    """devkit() turns the overlay on for its own run only: the next game
    starts as Settings says (off), not as the last one left it (Overbit's
    Select left it on in every game after, 2026-10-05)"""
    q = Qemu(b("kernel.img"))
    try:
        q.expect(MENU, timeout=30)
        time.sleep(0.5)
        for _ in range(2):
            assert _upload(q, mkbm.pack(DEVKIT_CART.encode(), title="devkit"))
            out = q.expect("devkit was ", timeout=30).decode(errors="replace")
            out += q.expect("\n", timeout=5).decode(errors="replace")
            assert "devkit was 0, now 2" in out, out[-300:]
            time.sleep(0.3)
            q.send("q")
            q.expect("update+draw", timeout=20)
            time.sleep(0.5)
    finally:
        q.close()


def test_bm_write(b, opts):
    """bm Write (carts/write, 2026-10-06): a document with a title and a bold
    word, saved in /docs: the console asks the first time for the documents
    (A allows, docs=yes in bm/config.txt); exported to PDF; the toolbar
    shows its name. The card is still a clean FAT32 volume and holds
    /DOCS/PROVA.BMD and PROVA.PDF."""
    tmp = tempfile.mkdtemp(prefix="bm-write-")
    img = os.path.join(tmp, "sd.img")
    cfg = os.path.join(tmp, "config.txt")
    with open(cfg, "w") as f:
        f.write("layout=us\nwifi_boot=0\n")
    mksd.build(img, [(cfg, "bm/config.txt")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])

    def k(s, gap=0.05):
        for c in re.findall(r"\x1b\[[0-9]*[~A-Z]|\x1bO[A-Z]|.", s, re.S):
            q.send(c)
            time.sleep(gap)

    try:
        q.boot()
        with open(b("carts/write.bm"), "rb") as f:
            assert _upload(q, f.read())
        q.expect("write: ready", timeout=30)
        time.sleep(0.5)
        k("\x1bOQProva\r")                             # F2: the title, Enter
        k("Una parola in \x02grassetto\x02.\r")        # Ctrl+B
        k("\x13", 0.4)                                  # Ctrl+S: the name
        k("PROVA\r")
        q.expect("asks for the documents (ok: allow, back: no)", timeout=15)
        time.sleep(0.3)
        if opts.shots:
            _save_png(q.screendump(), os.path.join(opts.shots, "write-permission.png"))
        q.send("j")                                     # A: allow
        q.expect("bm: allowed", timeout=10)
        q.expect("write: saved /docs/prova.bmd (3 paragraphs)", timeout=15)
        _, text = settled_screen(q, lambda i, t: any("prova.bmd" in l for l in t))
        assert any("bm Write" in l and "prova.bmd" in l for l in text), "\n".join(text)
        k("\x1b", 0.7)                                  # Esc: the menu, Export..., PDF
        k("\x1b[B\x1b[B\x1b[B\x1b[B\r", 0.2)
        k("\r", 0.2)
        q.expect("write: exported /docs/prova.pdf", timeout=20)
        if opts.shots:
            _save_png(q.screendump(), os.path.join(opts.shots, "write.png"))
        q.send("\x1c")                                  # Ctrl+\: Ctrl+Esc, saved: it leaves
        q.expect("> ", timeout=20)
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
        bmd = subprocess.run(["mtype", "-i", part, "::/DOCS/PROVA.BMD"], capture_output=True, env=env).stdout
        assert bmd.startswith(b"bmwrite 1\n") and b"title center |Prova\n" in bmd, bmd
        assert b"body left 15:9:1|Una parola in grassetto." in bmd, bmd
        pdf = subprocess.run(["mtype", "-i", part, "::/DOCS/PROVA.PDF"], capture_output=True, env=env).stdout
        assert pdf.startswith(b"%PDF-1.4") and b"(Prova) Tj" in pdf and pdf.rstrip().endswith(b"%%EOF"), pdf[:200]
        txt = subprocess.run(["mtype", "-i", part, "::/BM/CONFIG.TXT"], capture_output=True,
                             text=True, env=env).stdout
        assert re.search(r"allow_[0-9A-F]{8}=docs=yes \(bm Write\)", txt), txt
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


PERMIT_CART = r"""
function _init()
  log("report1", report("test", "hello"))
  log("report2", report("test", "again"))
  quit()
end
"""


def test_permissions(b, opts):
    """The first time a game uses report() (or the network) the player is
    asked, over the game stopped in its call; the answer stays in
    bm/config.txt for that game (allow_<save name>): asked once, kept
    across runs, a no refused (report() false) without asking again
    (decision of the user, 2026-10-05)"""
    tmp = tempfile.mkdtemp(prefix="bm-perm-")
    img = os.path.join(tmp, "sd.img")
    cfg = os.path.join(tmp, "config.txt")
    with open(cfg, "w") as f:
        f.write("layout=us\nwifi_boot=0\n")
    mksd.build(img, [(cfg, "bm/config.txt")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    try:
        q.boot()

        def run(title, key):
            assert _upload(q, mkbm.pack(PERMIT_CART.encode(), title=title))
            out = b""
            if key:
                out += q.expect("asks to send a report (ok: allow, back: no)", timeout=30)
                time.sleep(0.3)
                if opts.shots:
                    _save_png(q.screendump(), os.path.join(opts.shots, f"permission-{title}.png"))
                q.send(key)
            out += q.expect("report2\t", timeout=30)
            out += q.expect("\n", timeout=5)
            text = out.decode(errors="replace").replace("\r", "")
            q.expect("> ", timeout=20)
            time.sleep(0.3)
            return text

        text = run("permit", "j")                   # A: allow
        assert "bm: allowed" in text and "report1\ttrue" in text and "report2\ttrue" in text, text
        text = run("deny", "k")                     # B: no
        assert "bm: not allowed" in text and "report1\tfalse" in text and "report2\tfalse" in text, text
        text = run("permit", None)                  # kept: not asked again
        assert "asks to send" not in text and "report1\ttrue" in text, text
        text = run("deny", None)
        assert "asks to send" not in text and "report1\tfalse" in text, text
    finally:
        q.close()
    try:
        part = os.path.join(tmp, "part.img")
        with open(img, "rb") as f, open(part, "wb") as o:
            f.seek(2048 * 512)
            o.write(f.read())
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
        txt = subprocess.run(["mtype", "-i", part, "::/BM/CONFIG.TXT"], capture_output=True,
                             text=True, env=env).stdout
        assert re.search(r"allow_[0-9A-F]{8}=report=yes \(permit\)", txt), txt
        assert re.search(r"allow_[0-9A-F]{8}=report=no \(deny\)", txt), txt
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


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
  log("bad", select(2, pcall(note, 8, 440)), select(2, pcall(apu, 0, 32)))
  log("nobank", sfx(0), music())
  tone(3, {wave = "saw", cutoff = 1000, res = 0.5, pan = -1, reverb = 0, attack = 100})
  log("tone", apu(3, 2), apu(3, 11), apu(3, 12), apu(3, 16), apu(3, 22), apu(3, 5))
  tone(4, "epiano")
  log("preset", apu(4, 2), FM, PLUCK, SUPERSAW, ORGAN, #instruments() > 30, instruments("drum")[1].name)
  local pv = play(nil, "kick", "C2", 200)
  log("play", pv, playing(pv))
  log("fx", math.floor(reverb(0.8) * 100 + 0.5), math.floor(echo(250) + 0.5), retro(), retro(true), retro(false))
  log("badtone", select(2, pcall(tone, 0, "nothing")), select(2, pcall(tone, 0, {bogus = 1})))
  -- notes at a time on the sound's clock (play_at, riff's scheduler)
  local t0 = audio_time()
  log("at", play_at(t0 + 0.5, "snare", "D3", 100, 0.8, 7), play_at(t0 + 0.6, {s = "kick", cutoff = 500}, 36),
      play_voices(), select(2, pcall(play_at, t0, "nothing")))
  play_cancel(7)
  log("atcancel", play_voices(), play_voices(1, 2), audio_time() >= t0)
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
    mksd.build(img, [(b("carts/pong.bm"), "bm/pong.bm")])
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


def test_sd_logs(b, opts):
    """The Pi saves boot logs and the last run to SD (2026-10-15, D19):
    bootlog.txt while booting, bootprev.txt on the next boot, and lastrun.txt
    before watchdog_reboot()."""
    tmp = tempfile.mkdtemp(prefix="bm-sdlog-")
    try:
        img = os.path.join(tmp, "sd.img")
        cfg = os.path.join(tmp, "config.txt")
        with open(cfg, "w") as f:
            f.write("layout=us\nwifi_boot=0\nnet_password=test-pass-4242\n")
        mksd.build(img, [(cfg, "bm/config.txt"), (b("carts/astrowing.bm"), "bm/game.bm")])
        q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
        try:
            q.expect(MENU, timeout=30)
            time.sleep(0.5)
            q.send("q")
            q.expect(PROMPT)
            q.expect("> ")
            q.send("l\n")
            q.expect("lua> ")
            q.send("print('pw ' .. 'test-pass-' .. 4242)\n")
            q.expect("pw test-pass-4242")
            q.send("exit()\n")
            q.expect("> ")
            q.send("r")
            q.expect("restarting: this run's log in bm/lastrun.txt", timeout=20)
            q.expect(MENU, timeout=30)
            q.expect("boot log saved to bm/bootlog.txt", timeout=20)
            time.sleep(1)
        finally:
            q.close()
        part = _sd_files(img)
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
        lastrun = subprocess.run(["mtype", "-i", part, "::/BM/LASTRUN.TXT"],
                                 capture_output=True, text=True, env=env, check=True).stdout
        assert "rebooting..." in lastrun, lastrun
        assert "test-pass-4242" not in lastrun, lastrun
        assert "pw **************" in lastrun, lastrun
        bootprev = subprocess.run(["mtype", "-i", part, "::/BM/BOOTPREV.TXT"],
                                  capture_output=True, text=True, env=env, check=True).stdout
        assert "--- the run before" in bootprev, bootprev
        assert "ended cleanly, or the power went" in bootprev, bootprev
        assert "while: boot" not in bootprev, bootprev
        assert "rebooting..." in bootprev, bootprev
        assert "test-pass-4242" not in bootprev, bootprev
        bootlog = subprocess.run(["mtype", "-i", part, "::/BM/BOOTLOG.TXT"],
                                 capture_output=True, text=True, env=env, check=True).stdout
        assert "cartridge menu" in bootlog, bootlog
        assert "test-pass-4242" not in bootlog, bootlog
        assert "watchdog: resets at once on RSTC (emulator), no freeze guard" in bootlog, bootlog
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def test_sound_editor(b, opts):
    """The Sound editor (Dev tab, monitor A): starts on the demo, types a
    note with the piano keys, saves the demo as a new sound pack, opens a
    game from the SD card, gives it a pattern and saves it into the game;
    the game then plays with its new bank, and the card is a clean FAT32
    volume. A game is read only: the save goes into its editable copy
    (PONG.BME), after the console's question."""
    tmp = tempfile.mkdtemp(prefix="bm-snd-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [(b("carts/pong.bm"), "bm/pong.bm")])
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
        q.expect("sound: saved /bme/demo.bme", timeout=20)     # a project
        k("\x0f", 1.5)                                      # Ctrl+O: the files
        k("\r")                                             # /bm/pong.bm
        q.expect("sound: opened /bm/pong.bm (no sounds yet)", timeout=20)
        k("\x1bOR")
        k("z")                                              # C4 on track 1, step 1
        k("\x13", 1)
        copy_yes(q, "/bm/pong.bm", "/bme/pong.bme")    # a game: its editable copy
        q.expect("sound: saved /bme/pong.bme", timeout=20)
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
        pong = subprocess.run(["mtype", "-i", part, "::/BME/PONG.BME"], capture_output=True, env=env).stdout
        assert pong[:8] == b"BMCART\x00\x00", pong[:16]
        assert pong[18] & 1, "the copy is a project (.bme)"
        game = subprocess.run(["mtype", "-i", part, "::/BM/PONG.BM"], capture_output=True, env=env).stdout
        assert game == open(b("carts/pong.bm"), "rb").read(), "the game stays as it was"
        import zlib
        assert zlib.crc32(pong[128:]) & 0xFFFFFFFF == int.from_bytes(pong[20:24], "little"), "pong.bm CRC"
        bank = bmaudio.unpack(bmaudio.extract(pong))
        assert bank["patterns"][0]["tracks"]["0"][0].startswith("C4"), bank["patterns"]
        assert b"function _update" in pong                  # its code is still there
        pack = subprocess.run(["mtype", "-i", part, "::/BME/DEMO.BME"], capture_output=True, env=env).stdout
        demo = bmaudio.unpack(bmaudio.extract(pack))
        assert demo["patterns"][0]["tracks"]["5"][0].startswith("C5"), demo["patterns"][0]
        assert len(demo["songs"]) == 3 and demo["songs"][0]["name"] == "DEMO", demo["songs"]
        hifi = demo["songs"][2]                             # the new voices' song: its echo and room
        assert hifi["name"] == "HIFI" and hifi.get("echo") == 3 and hifi.get("room") == 120, hifi
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


def test_sound_assistant(b, opts):
    """The music assistant in bm Sound (F6, src/ai/music.c): a lo-fi backing
    track for the words goes into new patterns with a song of its own, then
    a coin into a free sound effect; the kernel's network chose them."""
    q = Qemu(b("kernel.img"))

    def k(s, gap=0.25):
        q.send(s)
        time.sleep(gap)
    try:
        q.boot()
        k("A", 3)                                           # bm Sound, on the demo
        k("\x1b[17~", 1.5)                                  # F6: the assistant
        for ch in "base lofi in re":
            k(ch, 0.08)
        time.sleep(0.8)
        if opts.shots:
            _save_png(q.screendump(), os.path.join(opts.shots, "sound-assistant.png"))
        k("\r", 1)
        out = q.expect("sound: assistant: base.lofi", timeout=20).decode(errors="replace")
        out += q.expect("\n").decode(errors="replace")
        assert "into song 3" in out, out                    # the demo has songs 0, 1 and 2
        k("\x1b[17~", 1.5)
        for ch in "effetto moneta":
            k(ch, 0.08)
        time.sleep(0.8)
        k("\r", 1)
        out = q.expect("sound: assistant: sfx.coin", timeout=20).decode(errors="replace")
        out += q.expect("\n").decode(errors="replace")
        assert "into sound effect 08" in out, out           # after the demo's eight
    finally:
        q.close()


def test_sound_riff(b, opts):
    """bm Sound's Riff... (F7, require "riff"): the first example (four on
    the floor) plays live, an arpeggio from the examples too, then
    Ctrl+Enter puts four bars of it into the bank as a song of its own."""
    q = Qemu(b("kernel.img"))

    def k(s, gap=0.25):
        q.send(s)
        time.sleep(gap)
    try:
        q.boot()
        k("A", 3)                                           # bm Sound, on the demo
        k("\x1b[18~", 1.5)                                  # F7: Riff...
        k("\r", 1)
        q.expect('sound: riff s "kick*4, ~ snare, hat*8"', timeout=10)
        for _ in range(3):
            k("\x1b[B", 0.3)                                # down: the arpeggio
        k("\r", 1)
        q.expect('sound: riff chord "<Am F C G>"', timeout=10)
        time.sleep(1)
        if opts.shots:
            _save_png(q.screendump(), os.path.join(opts.shots, "sound-riff.png"))
        k("\x1b[28~", 1)                                    # Ctrl+Enter: into the bank
        out = q.expect("sound: riff into song", timeout=10).decode(errors="replace")
        out += q.expect("\n").decode(errors="replace")
        assert "into song 3, 1 patterns, 120 BPM" in out, out   # the demo has songs 0, 1 and 2
    finally:
        q.close()


def test_sound_import(b, opts):
    """bm Sound imports a WAV and a MIDI file from /docs on the SD card (Ctrl+W): the
    tool reads the documents without asking; the MIDI file becomes a song, the WAV a
    sample of the sound; the bank saved as a pack reads back with both."""
    import math
    import struct
    import wave
    tmp = tempfile.mkdtemp(prefix="bm-snd-imp-")
    img = os.path.join(tmp, "sd.img")
    cfg = os.path.join(tmp, "config.txt")
    with open(cfg, "w") as f:
        f.write("layout=us\nwifi_boot=0\n")
    wav_path = os.path.join(tmp, "tone.wav")
    with wave.open(wav_path, "wb") as w:                    # 441 frames of a 1 kHz sine, mono, 22050 Hz
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(22050)
        w.writeframes(b"".join(struct.pack("<h", int(12000 * math.sin(2 * math.pi * i / 22.05))) for i in range(441)))

    def vlq(v):
        out = [v & 127]
        v >>= 7
        while v:
            out.insert(0, v & 127 | 128)
            v >>= 7
        return bytes(out)
    ev = [(0, b"\xff\x51\x03\x07\xa1\x20"), (0, b"\xff\x58\x04\x04\x02\x18\x08")]   # 500000 us, 4/4
    for tick, dur, key, ch in ((0, 96, 60, 0), (96, 96, 64, 0), (192, 96, 67, 0), (0, 10, 36, 9), (96, 10, 38, 9)):
        ev.append((tick, bytes([0x90 | ch, key, 100])))
        ev.append((tick + dur, bytes([0x80 | ch, key, 0])))
    ev.sort(key=lambda e: e[0])
    body, at = b"", 0
    for tick, data in ev:
        body += vlq(tick - at) + data
        at = tick
    body += b"\x00\xff\x2f\x00"
    mid_path = os.path.join(tmp, "tune.mid")
    with open(mid_path, "wb") as f:
        f.write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, 96) + b"MTrk" + struct.pack(">I", len(body)) + body)
    mksd.build(img, [(cfg, "bm/config.txt"), (wav_path, "docs/tone.wav"), (mid_path, "docs/tune.mid")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])

    def k(s, gap=0.3):
        q.send(s)
        time.sleep(gap)
    try:
        q.boot()
        k("A", 3)                                           # bm Sound, on the demo
        k("\x17", 1.5)                                      # Ctrl+W: the list of /docs (a tool needs no permission)
        k("\x1b[B", 0.4)                                    # the list: tone.wav, tune.mid
        k("\r", 1)
        q.expect("sound: imported the MIDI tune.mid: song 3: 1 patterns, 120 BPM, 3 new sounds", timeout=15)
        k("\x17", 1)
        k("\r", 1)                                          # tone.wav
        k("\r", 1)                                          # as it is
        q.expect("sound: imported the WAV tone.wav as sample 0 TONE: 22050 Hz mono", timeout=15)
        if opts.shots:
            time.sleep(0.5)
            _save_png(q.screendump(), os.path.join(opts.shots, "sound-import.png"))
        k("\x13", 1)                                        # Ctrl+S: a new pack, named
        k("\r")
        q.expect("sound: saved /bme/demo.bme", timeout=20)
        k("\x1b", 0.6)                                      # the menu: Exit is the last item
        k("\x1b[6~")
        k("\x1b[6~")
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
        pack = subprocess.run(["mtype", "-i", part, "::/BME/DEMO.BME"], capture_output=True, env=env).stdout
        bank = bmaudio.unpack(bmaudio.extract(pack))
        assert len(bank["samples"]) == 1, bank.get("samples")
        smp = bank["samples"][0]
        assert smp["name"] == "TONE" and smp["rate"] == 22050 and smp["channels"] == 1 and smp["format"] == "s16", smp
        assert bank["sounds"][0].get("sample") == "TONE", bank["sounds"][0]
        song = bank["songs"][3]
        assert song["name"] == "TUNE" and song["bpm"] == 120, bank["songs"]
        pat = bank["patterns"][song["order"][0]]
        assert pat["tracks"]["0"][0].startswith("C4") and pat["tracks"]["0"][4].startswith("E4"), pat["tracks"]
        # the documents stay as they were
        got = subprocess.run(["mtype", "-i", part, "::/DOCS/TUNE.MID"], capture_output=True, env=env).stdout
        assert got == open(mid_path, "rb").read(), "the MIDI file on the card is unchanged"
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


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
        assert "bad\t" in out and "voice 0..7" in out and "register 0..31" in out, out
        out = q.expect("nobank\t", timeout=5).decode(errors="replace")
        out += q.expect("\n").decode(errors="replace")
        assert "nobank\tnil\tnil" in out, out
        # the tone in plain units, the instruments, play(), room, echo, retro
        out = q.expect("badtone\t", timeout=5).decode(errors="replace")
        out += q.expect("\n").decode(errors="replace")
        assert "tone\t2\t145\t128\t129\t0\t13" in out, out
        assert "preset\t6\t6\t7\t8\t9\ttrue\tkick" in out, out
        assert "play\t7\ttrue" in out, out
        assert "fx\t80\t250\tfalse\ttrue\tfalse" in out, out
        assert 'no instrument called "nothing"' in out and 'bad key or value "bogus"' in out, out
        out = q.expect("atcancel\t", timeout=5).decode(errors="replace")
        out += q.expect("\n").decode(errors="replace")
        assert "at\ttrue\ttrue\t2\t" in out and out.count('no instrument called "nothing"') >= 1, out
        assert "atcancel\t1\t1\ttrue" in out, out
        q.expect("> ", timeout=10)
    finally:
        q.close()


VIDEO_CART = r"""
local c, n
function _init()
  local err
  c, err = video_open("test.bmv")
  if not c then log("open failed " .. tostring(err)) return end
  log(string.format("info %d %d %d %d %d %s", c:info()))
  log("frame_at " .. c:frame_at(0) .. " " .. c:frame_at(70) .. " " .. c:frame_at(9999))
  log("missing " .. tostring(select(2, video_open("nope.bmv"))))
  log("badname " .. tostring(pcall(video_open, "../x")))
  n = 0
end
function _draw()
  cls(0)
  if not c then return end
  c:draw(2, 0, 0)
  n = n + 1
  if n == 3 then
    for _, p in ipairs({ { 20, 20 }, { 160, 90 }, { 300, 40 }, { 100, 150 }, { 250, 160 } }) do
      log(string.format("px %d %d %06X", p[1], p[2], pget(p[1], p[2])))
    end
    log("again " .. tostring(c:draw(0, 0, 0, 1)))
    log("badframe " .. tostring(pcall(c.draw, c, 3, 0, 0)))
    c:close()
    log("closed " .. tostring(pcall(c.info, c)))
    log("clip done")
  end
end
"""

# the test frame (a 320x180 baseline JPEG) from tests/video/vidframes.c, and its colours at five points (ffmpeg's decode)
VIDEO_PIXELS = {(20, 20): (40, 32, 15), (160, 90): (87, 79, 104), (300, 40): (86, 91, 106),
                (100, 150): (97, 98, 128), (250, 160): (18, 12, 8)}


def _bench_jpeg():
    src = open(os.path.join(HERE, "video", "vidframes.c")).read()
    body = src.split("vidframe_jpg320[] = {")[1].split("};")[0]
    return bytes(int(x) for x in re.findall(r"\d+", body))


def test_video_player(b, opts):
    """The clip player of the cartridge API (VID-FMT slice 2): video_open of a .bmv in /bm/clips, clip:info,
    frame_at, draw (the colours at five points match the picture), the same frame again, a bad frame and a
    bad name refused, close."""
    import mkvid
    tmp = tempfile.mkdtemp(prefix="bm-vidplay-")
    try:
        jpg = _bench_jpeg()
        clip = os.path.join(tmp, "test.bmv")
        with open(clip, "wb") as f:
            f.write(mkvid.pack([jpg, jpg, jpg], 15))
        cart = os.path.join(tmp, "vid.bm")
        with open(cart, "wb") as f:
            f.write(mkbm.pack(VIDEO_CART.encode(), title="Vid test", res=(320, 180)))
        cfg = os.path.join(tmp, "config.txt")
        with open(cfg, "w") as f:
            f.write("layout=us\nwifi_boot=0\n")
        img = os.path.join(tmp, "sd.img")
        mksd.build(img, [(cfg, "bm/config.txt"), (cart, "bm/vid.bm"), (clip, "bm/clips/test.bmv")])
        q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
        try:
            q.expect(MENU, timeout=30)
            time.sleep(0.5)
            q.send(":play vid\r")
            out = q.expect("clip done", timeout=60).decode(errors="replace")
            plain = re.sub(r"\x1b\[[0-9;]*m", "", out)
            assert "info 320 180 15 3 200 false" in plain, plain
            assert "frame_at 0 1 2" in plain, plain
            assert "missing no such clip" in plain, plain
            assert "badname false" in plain, plain
            assert "again true" in plain and "badframe false" in plain and "closed false" in plain, plain
            for (x, y), want in VIDEO_PIXELS.items():
                m = re.search(r"px %d %d ([0-9A-F]{6})" % (x, y), plain)
                assert m, plain
                got = tuple(int(m.group(1)[i:i + 2], 16) for i in (0, 2, 4))
                assert all(abs(g - w) <= 16 for g, w in zip(got, want)), (x, y, got, want)
        finally:
            q.close()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


REC_CART = r"""
local n = 0
function _draw()
  n = n + 1
  cls(rgb((n * 7) % 256, (n * 13) % 256, 128))
  if SAVE_EVERY_FRAME then save({ n = n }) end
  if time() > 2.2 then quit() end
end
"""

PLAY_REC_CART = r"""
local c, n
function _init()
  local err
  c, err = video_open("rec/r0010001.bmv")
  if not c then log("open failed " .. tostring(err)) return end
  log(string.format("info %d %d %d %d", c:info()))
  n = 0
end
function _draw()
  cls(0)
  if not c then return end
  local w, h, fps, frames = c:info()
  n = n + 1
  if n <= frames then
    c:draw(n - 1, 0, 0)
    log(string.format("frame %d %06X", n - 1, pget(10, 10)))
  elseif n == frames + 1 then
    log("play done")
  end
end
"""


def _rgb565_to_888(c):
    r, g, b = c >> 11, c >> 5 & 63, c & 31
    return (r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2)


def _rle565_decode(data, w, h):
    """The pixels of one RLE565 frame (docs/BM-VIDEO.md), written apart from the C decoder; asserts on a bad stream"""
    px, i = [], 0
    while len(px) < w * h:
        c = data[i]
        i += 1
        n, kind = (c & 63) + 1, c >> 6
        if kind == 0:
            px += struct.unpack("<%dH" % n, data[i:i + 2 * n])
            i += 2 * n
        elif kind == 1:
            px += [struct.unpack("<H", data[i:i + 2])[0]] * n
            i += 2
        else:
            assert kind == 2 and len(px) >= w, (kind, len(px))
            px += [px[len(px) - w + k] for k in range(n)]
    assert len(px) == w * h and i == len(data), (len(px), i, len(data))
    return px


def _bmv_frames(data):
    """(width, height, fps, [colour of each frame, or None if it is not one colour]) of an RGB565 or RLE565 .bmv chunk"""
    assert data[:4] == b"BMV1" and data[4] == 1 and data[5] in (2, 4), data[:8]
    w, h, fps = struct.unpack("<HHH", data[8:14])
    frames, index_off = struct.unpack("<II", data[16:24])
    cols = []
    for k in range(frames):
        off, size = struct.unpack("<II", data[index_off + 8 * k:index_off + 8 * k + 8])
        assert off + size <= len(data)
        if data[5] == 2:
            assert size == w * h * 2
            px = struct.unpack("<%dH" % (w * h), data[off:off + size])
        else:
            px = _rle565_decode(data[off:off + size], w, h)
        cols.append(px[0] if all(p == px[0] for p in px) else None)
    return w, h, fps, cols


def _rec_session(b, cart_src, config, tmp):
    cfg = os.path.join(tmp, "config.txt")
    with open(cfg, "w") as f:
        f.write("layout=us\nwifi_boot=0\n" + config)
    cart = os.path.join(tmp, "rec.bm")
    with open(cart, "wb") as f:
        f.write(mkbm.pack(cart_src.encode(), title="Rec test", res=(320, 180)))
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [(cfg, "bm/config.txt"), (cart, "bm/rec.bm")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    try:
        q.expect(MENU, timeout=30)
        time.sleep(0.5)
        q.send(":play rec\r")
        out = q.expect("chunks.", timeout=120).decode(errors="replace")
        time.sleep(1)
    finally:
        q.close()
    return img, re.sub(r"\x1b\[[0-9;]*m", "", out)


def _rec_chunks(img):
    part = _sd_files(img)
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    ls = subprocess.run(["mdir", "-b", "-i", part, "::/BM/REC"], capture_output=True, text=True, env=env).stdout
    names = sorted(re.findall(r"R\d{7}\.BMV", ls.upper()))
    chunks = []
    for nme in names:
        chunks.append(subprocess.run(["mtype", "-i", part, "::/BM/REC/" + nme], capture_output=True, env=env,
                                     check=True).stdout)
    return names, chunks


REC_DONE = r"run 1 done: (\d+) frames in [\d.]+ s, dropped (\d+) \(ring full\), (\d+) chunks \((\d+) failed\)"


def test_video_rec(b, opts):
    """The screen recorder (VID-REC, rec=on) with raw RGB565 frames: 2 s of a cart at 15 fps are written as .bmv
    chunks in /bm/rec, the report line has the counters, every frame is one colour and consecutive frames differ;
    then a cart plays the first chunk through video_open("rec/...") and sees the same colours."""
    _rec_check(b, "rec_format=rgb565\n", 2)


def test_video_rec_rle(b, opts):
    """The same with the default format, RLE565 (lossless run-length frames): the chunks hold many small frames,
    the report has the bytes and the ms of a frame, and the cart's player sees the same colours."""
    out = _rec_check(b, "", 1)
    assert re.search(r"rec: rle565, \d+ bytes a frame \(mean\), \d+ largest, encode \d+\.\d\d ms a frame \(mean\)", out), out


def _rec_check(b, fmt_cfg, min_chunks):
    tmp = tempfile.mkdtemp(prefix="bm-rec-")
    try:
        img, out = _rec_session(b, "SAVE_EVERY_FRAME = false\n" + REC_CART,
                                "rec=on\nrec_fps=15\nrec_ring_mb=8\n" + fmt_cfg, tmp)
        m = re.search(REC_DONE, out)
        assert m, out
        captured, dropped, nchunks, failed = (int(x) for x in m.groups())
        assert captured >= 8 and failed == 0 and nchunks >= min_chunks, out
        assert re.search(r"card \d+\.\d\d MB/s while writing, worst chunk [\d.]+ ms, game waited at most [\d.]+ ms", out), out
        names, chunks = _rec_chunks(img)
        assert len(chunks) == nchunks and names[0] == "R0010001.BMV", (names, out)
        cols = []
        for data in chunks:
            w, h, fps, c = _bmv_frames(data)
            assert (w, h, fps) == (320, 180, 15), (w, h, fps)
            cols += c
        assert len(cols) == captured, (len(cols), captured)
        assert all(c is not None for c in cols), cols
        assert all(a != b_ for a, b_ in zip(cols, cols[1:])), cols
        # the first chunk through the cart's player (video_open("rec/r0010001.bmv"))
        first = os.path.join(tmp, "c1.bmv")
        with open(first, "wb") as f:
            f.write(chunks[0])
        pcart = os.path.join(tmp, "play.bm")
        with open(pcart, "wb") as f:
            f.write(mkbm.pack(PLAY_REC_CART.encode(), title="Rec play", res=(320, 180)))
        img2 = os.path.join(tmp, "sd2.img")
        mksd.build(img2, [(os.path.join(tmp, "config.txt"), "bm/config.txt"), (pcart, "bm/play.bm"),
                          (first, "bm/rec/r0010001.bmv")])
        q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img2}"])
        try:
            q.expect(MENU, timeout=30)
            time.sleep(0.5)
            q.send(":play play\r")
            out2 = q.expect("play done", timeout=60).decode(errors="replace")
        finally:
            q.close()
        plain2 = re.sub(r"\x1b\[[0-9;]*m", "", out2)
        first_cols = _bmv_frames(chunks[0])[3]
        assert "open failed" not in plain2, plain2
        got = re.findall(r"frame (\d+) ([0-9A-F]{6})", plain2)
        assert len(got) == len(first_cols), (len(got), len(first_cols), plain2)
        for (k, hexc), want in zip(got, first_cols):
            assert int(hexc, 16) == _rgb565_to_888(want), (k, hexc, want)
        return out
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def test_video_rec_drops(b, opts):
    """A game that saves to the card on every frame keeps the recorder from writing (the card is the game's
    for 300 ms after each save): the ring of 1 MB (3 chunks) fills, frames are dropped and counted, nothing
    blocks, and the chunks are all written when the game ends."""
    tmp = tempfile.mkdtemp(prefix="bm-recdrop-")
    try:
        img, out = _rec_session(b, "SAVE_EVERY_FRAME = true\n" + REC_CART, "rec=on\nrec_fps=15\nrec_ring_mb=1\nrec_format=rgb565\n", tmp)
        m = re.search(REC_DONE, out)
        assert m, out
        captured, dropped, nchunks, failed = (int(x) for x in m.groups())
        assert dropped >= 1 and failed == 0 and captured >= 6, out
        assert re.search(r"ring high 3 of 3 chunks.", out), out
        names, chunks = _rec_chunks(img)
        assert len(chunks) == nchunks == (captured + 2) // 3, (len(chunks), nchunks, captured)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def test_bench_stop(b, opts):
    """2026-10-10: the rendering tests stop on the keys that leave a game
    (src/kernel/syskeys.h: Start+Select, Ctrl+Esc, PS), here Ctrl+Esc on
    the USB keyboard: the 3D Bench in its ramps (no report, no pages); the
    monitor goes on after it, the PS not taken as its own."""
    q = Qemu(b("kernel.img"), USB_KBD)
    try:
        q.boot()
        time.sleep(0.5)
        q.send("j")
        q.expect("b3d spheres ARM n=", timeout=60)       # past the boot's settling: a ramp
        sendkeys(q, "ctrl-esc")
        out = q.expect("3D Bench: stopped; no report saved or sent", timeout=20).decode(errors="replace")
        out += q.expect("> ", timeout=10).decode(errors="replace")
        assert "b3d stopped by the user" in out and "3D Bench: done" not in out, out
        assert "report:" not in out, out
    finally:
        q.close()


# a textured quad that keeps drawing (a sheet of 256 colours); the fallback test stops it with q
FALLBACK_CART = r"""
local m
function _init()
  for y = 0, 15 do for x = 0, 15 do sset(x, y, (x * 16) * 65536 + (y * 16) * 256 + 128) end end
  m = mesh({ -1,-1,0, 1,-1,0, 1,1,0, -1,1,0 }, { 1,3,2,-1, 1,4,3,-1 },
           { 0,16, 16,0, 16,16,   0,16, 0,0, 16,0 })
end
function _draw()
  cls(0)
  zclear()
  camera3d(0, 0, -2)
  light3d(0, 0, -1, 1)
  draw3d(m, 0, 0, 0)
end
"""


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
        assert _upload(q, mkbm.pack(FALLBACK_CART.encode(), title="fallback test"))
        out = q.expect("bm: the 3D is drawn by the ARM as bm3d 0.2: ", timeout=20).decode(errors="replace")
        q.expect("no V3D answers", timeout=5)
        for _ in range(20):                     # the quad, with its texture (after _init)
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
  local p = assert(cart_load("/bm/MODELS.BM"))
  log("loaded", #models(), models()[1], p.title)
  p.title = "copy"
  log("saved", tostring(cart_save("/bm/COPY.BM", p)))
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
    """bm Animator: a model with a skeleton (ANIM) moves on the console:
    animate() poses it (an arm turns up around its shoulder), clips() lists
    the animations, bone3d() follows a bone, two clips mix."""
    cart = anim_figure.cart(ANIM_CART.encode(), title="anim test")
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
    mksd.build(img, [(src, "bm/models.bm")])
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
        saved = subprocess.run(["mtype", "-i", part, "::/BM/COPY.BM"], capture_output=True, env=env).stdout
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


DEBUG_CART = r"""local n = 0
function add(a, b)
  local s = a + b
  return s
end
function _update()
  n = add(n, 1)
  if n == 30 then breakpoint("thirty") end
end
function _draw()
  cls(0x102030)
  print("n " .. n, 8, 8, 0xFFFFFF)
end
"""


def test_debugger(b, opts):
    """R13, the debugger: in bm Code F8 puts a breakpoint on line 3, F5 runs
    the game, which stops there (the code, the variables a and b, the stack
    add < _update); A the next line (s = 1), Y out of add (back in
    _update), Start goes on to the breakpoint again, Select stops the game
    and bm Code comes back on the line; then without breakpoints
    breakpoint("thirty") stops it at n = 30 (an upvalue), Start goes on."""
    tmp = tempfile.mkdtemp(prefix="bm-debug-")
    img = os.path.join(tmp, "sd.img")
    cart = os.path.join(tmp, "dbg.bm")
    with open(cart, "wb") as f:
        f.write(mkbm.pack(DEBUG_CART.encode(), title="debug me", author="tests"))
    mksd.build(img, [(cart, "bm/DBG.BM")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])

    def k(s, gap=0.04):
        for c in re.findall(r"\x1b\[[0-9]*[~A-Z]|\x1bO[A-Z]|.", s, re.S):
            q.send(c)
            time.sleep(gap)

    def expect(needle, timeout=20):
        return q.expect(needle, timeout=timeout).decode(errors="replace")

    def see(words, font=(8, 16), tries=40):
        text = []
        for _ in range(tries):
            text = screen_text(q.screendump(), *font)
            if all(any(w in l for l in text) for w in words):
                return text
            time.sleep(0.25)
        raise AssertionError(f"not on screen: {words}\n" + "\n".join(text))

    try:
        q.boot()
        k("C")
        expect("code: ready")
        time.sleep(0.5)
        k("\x0f", 0.5)                                     # Ctrl+O: the files
        k("\r")
        expect("code: opened /bm/DBG.BM")
        k("\x0c")                                          # Ctrl+L: line 3
        k("3\r", 0.1)
        k("\x1b[19~", 0.3)                                 # F8: a breakpoint
        expect("code: breakpoints 3")
        see(["breakpoint on line 3"], (6, 12))
        k("\x1b[15~")                                      # F5: run
        expect("code: run /bm/DBG.BM (breakpoints 3)")
        out = expect("debug: breakpoint at main.lua:3 in add")
        out += expect("debug:   b = 1")
        assert "debug:   a = 0" in out, out
        text = see(["DEBUG", "main.lua:3", "variables of add", "a = 0", "b = 1", "stack", "add  3", "_update  7"])
        assert any(l.startswith(">*   3   local s = a + b") for l in text), "\n".join(text)
        if opts.shots:
            _save_png(q.screendump(), os.path.join(opts.shots, "debugger.png"))
        k("j")                                              # A: the next line
        out = expect("debug: step at main.lua:4 in add")
        out += expect("debug:   s = 1")
        k("v")                                              # Y: out of add
        expect("debug: step at main.lua:8 in _update")
        see(["variables of _update", "n = 1", "_update  8"])   # n: an upvalue, in blue
        k("\r")                                            # Start: go on, to the breakpoint again
        expect("debug: go on")
        out = expect("debug: breakpoint at main.lua:3 in add")
        out += expect("debug:   b = 1")
        assert "debug:   a = 1" in out, out
        k("\t")                                            # Select: stop the game
        expect("debug: stopped at main.lua:3")
        expect("code: ready", timeout=30)
        text = see(["stopped in the debugger at line 3"], (6, 12))
        k("\x1b[19~", 0.3)                                 # F8 on line 3 again: off
        expect("code: breakpoints none")
        k("\x1b[15~")                                      # F5: breakpoint("thirty") stops it
        expect("code: run /bm/DBG.BM")
        out = expect("debug: thirty at main.lua:8 in _update")
        out += expect("debug:   ^n = 30")
        see(["DEBUG", "thirty", "n = 30"])
        k("\r")                                            # Start: go on
        expect("debug: go on")
        time.sleep(0.5)
        k("q")
        expect("code: ready", timeout=30)
    finally:
        q.close()
        shutil.rmtree(tmp, ignore_errors=True)


def test_editor_mouse(b, opts):
    """The mouse in the editor suite (bmui, 2026-10-06; QEMU's USB tablet):
    in bm Code a click puts the cursor on the line and column under it, a
    double click takes the word (typing replaces it), the wheel scrolls the
    page, a click in the numbers is a breakpoint, a click on a tab shows it,
    the right button opens the context menu with the keys of its entries and
    a click on one does it (Undo); in the bm SDK a click on a tab changes
    page, the right button opens its menu and a click outside closes it."""
    tmp = tempfile.mkdtemp(prefix="bm-emouse-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [(b("carts/pong.bm"), "bm/pong.bm")])
    args = ["-drive", f"if=sd,format=raw,file={img}", "-device", "usb-tablet,port=1"]

    def run(q, s, gap=0.05):
        for c in re.findall(r"\x1b\[[0-9]*[~A-Z]|\x1bO[A-Z]|.", s, re.S):
            q.send(c)
            time.sleep(gap)

    def look(q, words, cw=6, ch=12, tries=40, gone=()):
        text = []
        for _ in range(tries):
            text = screen_text(q.screendump(), cw, ch)
            joined = "\n".join(text)
            if all(w in joined for w in words) and not any(g in joined for g in gone):
                return text
            time.sleep(0.25)
        raise AssertionError(f"not on screen: {words} (gone: {gone})\n" + "\n".join(text))

    def click(q, x, y, button="left", times=1):
        q.pointer(x + 1, y)                 # a move first: the arrow shows (keys hide it)
        time.sleep(0.15)
        q.pointer(x, y)
        time.sleep(0.25)
        for _ in range(times):            # (a double click: quick, as a hand does it)
            for down in (True, False):
                q.input_events([{"type": "btn", "data": {"down": down, "button": button}}])
                time.sleep(0.05 if times > 1 else 0.1)
        time.sleep(0.3)

    def find(text, word, cw, ch):
        for r, line in enumerate(text):
            c = line.find(word)
            if c >= 0:
                return c * cw + cw // 2, r * ch + ch // 2
        raise AssertionError(f"{word!r} not on screen:\n" + "\n".join(text))

    def shot(q, name):
        if opts.shots:
            _save_png(q.screendump(), os.path.join(opts.shots, f"emouse-{name}.png"))

    q = Qemu(b("kernel.img"), args)
    try:
        q.boot()
        run(q, "C")
        q.expect("code: ready", timeout=30)
        run(q, "\x0f", 0.5)                 # Ctrl+O, the first file
        look(q, ["/bm/pong.bm"])
        run(q, "\r")
        q.expect("code: opened /bm/pong.bm", timeout=15)
        # a click on line 3, between columns 5 and 6 (6x12: the text from x 24)
        click(q, 24 + 5 * 6, 12 + 2 * 12 + 6)
        look(q, ["ln 3/", "col 6"])
        # a double click on "local" of line 5, then a letter replaces it
        click(q, 24 + 2 * 6 + 3, 12 + 4 * 12 + 6, times=2)
        run(q, "X")
        look(q, ["  5 X", "W, H = SCREEN_W"])        # (the completion's grey rest after the X)
        shot(q, "word")
        # the context menu (8x16, on its grid): Undo takes the X away
        click(q, 300, 120, "right")
        shot(q, "context")
        text = look(q, ["Paste", "Ctrl+V", "Undo", "Ctrl+Z"], 8, 16)
        ux, uy = find(text, "Undo", 8, 16)
        click(q, ux, uy)
        look(q, ["W, H = SCREEN_W"], gone=["  5 X", "Ctrl+V"])
        # a click in the numbers: a breakpoint on line 7
        click(q, 6, 12 + 6 * 12 + 6)
        look(q, ["breakpoint on line 7"])
        # the wheel: three lines a click, the cursor stays
        click(q, 300, 200, "wheel-down")
        text = look(q, ["ln 7/"])
        assert text[1].lstrip().startswith("4"), text[1]
        # a new tab (Ctrl+T), then a click on pong's tab shows it again
        run(q, "\x14", 0.5)
        look(q, ["untitled"])
        text = screen_text(q.screendump(), 6, 12)
        tx, ty = find(text[:1], "pong.bm", 6, 12)
        click(q, tx, ty)
        look(q, ["/bm/pong.bm"])
        shot(q, "code")
    finally:
        q.close()

    # the SDK: the tabs by clicking, its context menu
    q = Qemu(b("kernel.img"), args)
    try:
        q.boot()
        run(q, "e")
        text = look(q, ["OPEN IN", "code"], 8, 16)   # the same dump: a later one may catch a redraw
        cx, cy = find(text[:1], "code", 8, 16)
        click(q, cx, cy)                    # the F2 tab: the code page
        look(q, ["line 1/"], 8, 16, gone=["OPEN IN"])
        click(q, 320, 200, "right")
        text = look(q, ["Cut the line", "Build the game .bm", "Ctrl+B"], 8, 16)
        shot(q, "sdk-context")
        click(q, 600, 40)                   # outside: it closes, nothing else
        text = look(q, ["menu"], 8, 16, gone=["Cut the line"])   # the same dump: a later one may catch a redraw
        mx, my = find(text[:1], "menu", 8, 16)
        click(q, mx, my)                    # the Esc tab: the menu page
        text = look(q, ["Continue", "Exit bm SDK"], 8, 16)
        ex, ey = find(text, "Continue", 8, 16)
        click(q, ex, ey)                    # a click on an entry does it
        look(q, ["line 1/"], 8, 16, gone=["Exit bm SDK"])
    finally:
        q.close()
        shutil.rmtree(tmp, ignore_errors=True)


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
    mksd.build(img, [(b("carts/mapkit.bm"), "bm/Il mio demo.bm"), (b("carts/pong.bm"), "bm/pong.bm")])
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

    def see(words, tries=80):
        # 20 s: a key sent after a serial line can wait for the editor's next
        # frame, slow on a busy runner; on a miss the screen goes to build/diag/
        text = []
        for _ in range(tries):
            img_ = q.screendump()
            text = screen_text(img_, 6, 12)
            if all(any(w in l for l in text) for w in words):
                return text
            time.sleep(0.25)
        os.makedirs("build/diag", exist_ok=True)
        _save_png(img_, "build/diag/code-editor-miss.png")
        with open("build/diag/code-editor-miss.txt", "w") as f:
            f.write(f"not on screen: {words}\n" + "\n".join(text))
        raise AssertionError(f"not on screen: {words}\n" + "\n".join(text))

    def copy_asked(q, game, copy):
        # the question's box is drawn after the first poll of its keys: an
        # answer sent before is "held at the start" and lost, a copy that
        # never ends on a busy runner. Wait for the box, then answer.
        wait_screen(q, lambda s_: "editable copy" in "".join(screen_text(s_)), timeout=20)
        copy_yes(q, game, copy)

    def shot(name):
        if opts.shots:
            _save_png(q.screendump(), os.path.join(opts.shots, f"code-{name}.png"))
    try:
        q.boot()
        k("C")
        expect("code: ready")
        see(["keys"])                                       # the F1 chip, then "keys"
        k("\x0f", 0.5)                                      # Ctrl+O: the files
        see(["Open a cartridge", "/bm/Il mio demo.bm", "/bm/pong.bm"])
        shot("open")
        k("\r")
        expect("code: opened /bm/Il mio demo.bm")
        k("\x0f", 0.5)
        see(["Open a cartridge", "/bm/pong.bm"])         # the list, before Down and Enter
        k("\x1b[B", 0.3)
        k("\r")
        expect("code: opened /bm/pong.bm")
        see(["pong.bm"])                                    # the tab is drawn, before F4
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
        copy_asked(q, "/bm/pong.bm", "/bme/pong.bme")    # a game: its editable copy
        expect("code: saved /bme/pong.bme")

        # the demo (long name, sprites): an error on line 1, then F5
        k("\x1bOQ", 0.3)                                    # F2: the other tab
        k("\x0c")
        k("1\r", 0.1)
        k('error("boom")\r')
        k("\x1b[15~")                                       # F5: save and run
        copy_asked(q, "/bm/Il mio demo.bm", "/bme/ilmiodem.bme")
        expect("code: saved /bme/ilmiodem.bme")
        expect("main.lua:1: boom", timeout=30)
        expect("code: ready", timeout=30)
        see(["the game stopped"])
        shot("error")
        k("\x1b[20~", 0.5)                                  # F9: explained
        see(["Assistant", "error"])
        k("\x1b", 0.6)                                     # the panel closes

        # a new tab, a function, an "#entry:" line above its if
        k("\x14", 0.3)                                      # Ctrl+T
        see(["untitled"])                                   # the new tab, before the typing
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

        # a new project: Ctrl+N, the name offered, Ctrl+S writes it
        k("\x0e", 0.5)                                      # Ctrl+N: the templates
        see(["choose a template", "Empty"])
        k("\r", 0.5)                                        # Empty
        see(["New project, file name", "game1.bme"])
        k("\r", 0.5)
        k("\x13")
        expect("code: saved /bme/game1.bme")

        # leave: the untitled tab has changes; kept for later
        k("\x1b", 0.6)                                     # Esc: the menu
        # the menu has more rows than the screen (MENU in carts/code/main.lua,
        # 27 entries): Exit, the last, is below the first window
        see(["New project"])
        k("\x1b[A", 0.3)                                   # up from the first: Exit
        see(["Exit"])
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
        # the games stay as they were: the saves went into their editable copies
        game = os.path.join(tmp, "game.bm")
        subprocess.run(["mcopy", "-i", part, "::/BM/Il mio demo.bm", game], check=True, env=env)
        assert open(game, "rb").read() == open(b("carts/mapkit.bm"), "rb").read(), "the demo (a game) changed"
        out = os.path.join(tmp, "demo.bm")
        subprocess.run(["mcopy", "-i", part, "::/BME/ILMIODEM.BME", out], check=True, env=env)
        sys.path.insert(0, os.path.join(HERE, "..", "scripts"))
        new = open(out, "rb").read()
        old = open(b("carts/mapkit.bm"), "rb").read()
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
        subprocess.run(["mcopy", "-i", part, "-o", "::/BME/PONG.BME", os.path.join(tmp, "pong.bm")],
                       check=True, env=env)
        assert open(os.path.join(tmp, "pong.bm"), "rb").read().find(b"-- edited by bm Code") > 0
        subprocess.run(["mcopy", "-i", part, "-o", "::/BME/GAME1.BME", os.path.join(tmp, "game1.bm")],
                       check=True, env=env)
        g = sections(open(os.path.join(tmp, "game1.bm"), "rb").read())
        assert list(g) == [1] and g[1].startswith(b"-- my game"), g.keys()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


MODS_LUA = "local n = 0\n--@module gfx\nlocal c = 1\n--@module ui\nlocal u = 2\n"


def test_code_modules(b, opts):
    """APP-8, bm Code: a project (.bme) whose code has "--@module" lines opens
    with a second row of tabs, one per module (main, gfx, ui); Ctrl+P moves to
    the next, a line typed in gfx is saved with Ctrl+S into the project's ONE
    code section (the other modules and their markers as they were), and
    the card is still a clean FAT32 volume."""
    tmp = tempfile.mkdtemp(prefix="bm-codemod-")
    img = os.path.join(tmp, "sd.img")
    src = os.path.join(tmp, "mods.bme")
    with open(src, "wb") as f:
        f.write(mkbm.mark_project(mkbm.pack(MODS_LUA.encode(), title="mods")))
    mksd.build(img, [(src, "bme/mods.bme")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}", "-device", "usb-tablet,port=1"])

    def click(x, y, button="left"):
        q.pointer(x + 1, y)                 # a move first: the arrow shows
        time.sleep(0.15)
        q.pointer(x, y)
        time.sleep(0.25)
        for down in (True, False):
            q.input_events([{"type": "btn", "data": {"down": down, "button": button}}])
            time.sleep(0.1)
        time.sleep(0.3)

    def at(text, row, word):
        c = text[row].find(word)
        assert c >= 0, (word, text[row])
        return c * 6 + 3, row * 12 + 6

    def k(s, gap=0.04):
        for c in re.findall(r"\x1b\[[0-9]*[~A-Z]|\x1bO[A-Z]|.", s, re.S):
            q.send(c)
            time.sleep(gap)

    def see(words, tries=80):
        text = []
        for _ in range(tries):
            img_ = q.screendump()
            text = screen_text(img_, 6, 12)
            if all(any(w in l for l in text) for w in words):
                return text
            time.sleep(0.25)
        os.makedirs("build/diag", exist_ok=True)
        _save_png(img_, "build/diag/code-modules-miss.png")
        raise AssertionError(f"not on screen: {words}\n" + "\n".join(text))
    try:
        q.boot()
        k("C")
        q.expect("code: ready", timeout=15)
        k("\x0f", 0.5)                                      # Ctrl+O
        see(["Open a cartridge", "/bme/mods.bme"])
        k("\r")
        q.expect("code: opened /bme/mods.bme", timeout=15)
        text = see(["modules", "main", "gfx", "ui", "local n = 0"])
        assert "mods.bme" in text[0], text[0]               # one file tab on the first row
        # the mouse: a click on a module tab shows it, the right button opens
        # its menu, the middle button closes no module (only a file tab does)
        click(*at(text, 1, "ui"))
        see(["local u = 2"])
        click(*at(text, 1, "gfx"), button="right")
        see(["local c = 1"])                                # the module under the pointer is shown
        click(400, 250)                                     # a click outside closes the menu
        click(*at(text, 1, "gfx"), button="middle")
        q.pointer(400, 300)                                 # the arrow off the tabs
        time.sleep(0.5)
        text = see(["modules", "gfx", "ui", "local c = 1"])
        click(*at(text, 1, "main"))
        q.pointer(400, 300)
        time.sleep(0.5)
        see(["local n = 0"])
        k("\x10", 0.5)                                      # Ctrl+P: the next module
        see(["local c = 1"])
        k("-- added\r")
        see(["-- added"])
        k("\x13")                                           # Ctrl+S: a project, no question
        q.expect("code: saved /bme/mods.bme", timeout=15)
        k("\x10", 0.5)                                      # ui
        see(["local u = 2"])
        k("\x10", 0.5)                                      # round to main
        see(["local n = 0"])
        # APP-23, find in the project: from the menu (Ctrl+Shift+F is its key); a
        # hit in the ui module opens that module
        k("\x1b", 0.6)                                      # Esc: the menu
        see(["Find in the project...", "Ctrl+Shift+F"])
        k("\x1b[B" * 13, 0.15)                              # down to "Find in the project..."
        k("\r", 0.5)
        see(["Find in the project:"])
        k("\b" * 12 + "U = 2", 0.1)                         # not case sensitive
        k("\r", 0.5)
        see(["Find \"U = 2\"", "1 hit", "ui:1 local u = 2"])
        k("\r", 0.6)                                        # Enter: go to the hit
        text = see(["local u = 2", "found at line 1"])
        assert not any("local n = 0" in l or "Find \"" in l for l in text), text
        k("\x10", 0.5)                                      # Ctrl+P: round to main
        see(["local n = 0"])
        # APP-24, rename a word: the menu row (Ctrl+Shift+H is its key); a
        # keyword is refused, a name changes as a whole word, Ctrl+Z brings it back
        k("\x1b[D" * 20, 0.2)                               # column 0: on "local"
        k("\x1b", 0.6)
        see(["Rename a word...", "Ctrl+Shift+H"])
        k("\x1b[B" * 14, 0.15)                              # down to "Rename a word..."
        k("\r", 0.5)
        see(["\"local\" is not a name"])
        k("\x1b[C" * 6, 0.2)                                # on the n of "local n = 0"
        k("\x1b", 0.6)
        k("\x1b[B" * 14, 0.15)
        k("\r", 0.5)
        see(["Rename \"n\" in the project, new name:"])
        k("\b" + "num", 0.1)
        k("\r", 0.5)
        see(["local num = 0", "1 renamed in 1 module"])
        k("\x1a", 0.4)                                      # Ctrl+Z: one undo step
        see(["local n = 0"])
        k("\x13")                                           # Ctrl+S: the same text again, no question at Exit
        q.expect("code: saved /bme/mods.bme", timeout=15)
        k("\x1b", 0.6)                                      # Esc: the menu
        see(["Modules..."])
        k("\x1b[A", 0.3)                                    # up from the first: Exit
        k("\r", 0.5)
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
        out = os.path.join(tmp, "out.bme")
        subprocess.run(["mcopy", "-i", part, "::/BME/MODS.BME", out], check=True, env=env)
        d = open(out, "rb").read()
        n = d[17]
        lua = None
        for i in range(n):
            t, off, size, _ = struct.unpack_from("<IIII", d, 128 + 16 * i)
            if t == 1:
                lua = d[off:off + size].decode()
        want = "local n = 0\n--@module gfx\n-- added\nlocal c = 1\n--@module ui\nlocal u = 2\n"
        assert lua == want, repr(lua)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def test_code_templates(b, opts):
    """R16, bm Code: Ctrl+N lists the templates (Empty, Platform 2D, Top-down 2D,
    Shooter 2D, Versus 2D, 3D scene); Esc leaves with no project; choosing
    Platform 2D asks for the name, opens a project with the module tabs
    (level, hero, loop) and Ctrl+S writes its code, markers included, into
    /bme/game1.bme. The card is still a clean FAT32 volume."""
    tmp = tempfile.mkdtemp(prefix="bm-codetpl-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [(b("carts/astrowing.bm"), "bm/demo.bm")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])

    def k(s, gap=0.04):
        for c in re.findall(r"\x1b\[[0-9]*[~A-Z]|\x1bO[A-Z]|.", s, re.S):
            q.send(c)
            time.sleep(gap)

    def see(words, tries=80):
        text = []
        for _ in range(tries):
            img_ = q.screendump()
            text = screen_text(img_, 6, 12)
            if all(any(w in l for l in text) for w in words):
                return text
            time.sleep(0.25)
        os.makedirs("build/diag", exist_ok=True)
        _save_png(img_, "build/diag/code-templates-miss.png")
        raise AssertionError(f"not on screen: {words}\n" + "\n".join(text))
    try:
        q.boot()
        k("C")
        q.expect("code: ready", timeout=15)
        k("\x0e", 0.5)                                      # Ctrl+N
        see(["choose a template", "Empty", "Platform 2D", "Top-down 2D", "Shooter 2D", "Versus 2D", "3D scene"])
        k("\x1b", 0.6)                                      # Esc: no project
        time.sleep(0.5)
        text = screen_text(q.screendump(), 6, 12)
        assert not any("choose a template" in l for l in text), text
        k("\x0e", 0.5)
        see(["choose a template"])
        k("\x1b[B", 0.3)                                    # down: Platform 2D
        k("\r", 0.5)
        see(["New project, file name", "game1.bme"])
        k("\r", 0.5)
        see(["platform: arrows run", "level", "hero", "loop"])
        k("\x13")                                           # Ctrl+S
        q.expect("code: saved /bme/game1.bme", timeout=15)
        k("\x1b", 0.6)                                      # Esc: the menu
        see(["Modules..."])
        k("\x1b[A", 0.3)                                    # up from the first: Exit
        k("\r", 0.5)
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
        out = os.path.join(tmp, "out.bme")
        subprocess.run(["mcopy", "-i", part, "::/BME/GAME1.BME", out], check=True, env=env)
        d = open(out, "rb").read()
        lua = None
        for i in range(d[17]):
            t, off, size, _ = struct.unpack_from("<IIII", d, 128 + 16 * i)
            if t == 1:
                lua = d[off:off + size].decode()
        assert lua and lua.startswith("-- platform: arrows run"), repr(lua and lua[:60])
        for m in ("--@module level", "--@module hero", "--@module loop"):
            assert m in lua, m
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


def test_riff_code(b, opts):
    """riff in bm Code (require "riff", M46): in an empty tab two patterns
    given to names, Ctrl+Enter (ESC [ 28 ~ on the serial line) plays them on
    the sound's clock: the status line says MUSIC 2, the words of the notes
    sounding light up; a mistake marks its line and says why, the music goes
    on; Ctrl+. (ESC [ 29 ~) stops it."""
    q = Qemu(b("kernel.img"))

    def k(s, gap=0.04):
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

    def coloured(rgb, tol=10):
        w, h, px = q.screendump()
        want = ((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255)
        n = 0
        for i in range(0, len(px), 3):
            if all(abs(px[i + j] - want[j]) <= tol for j in range(3)):
                n += 1
        return n

    def shot(name):
        if opts.shots:
            _save_png(q.screendump(), os.path.join(opts.shots, f"riff-{name}.png"))
    try:
        q.boot()
        k("C")
        q.expect("code: ready", timeout=15)
        see(["keys"])
        k("\x14", 0.4)                                      # Ctrl+T: an empty tab
        k('drums = s "kick*4, ~ snare, hat*8"\r')
        k('bass = note "<c2 a1 f1 g1>" :s "acid" :lpf(800)')
        see(["  2 bass"])
        k("\x1b[28~", 0.3)                                  # Ctrl+Enter
        q.expect("code: riff drums bass", timeout=10)
        see(["MUSIC 2"])
        lit = 0
        for _ in range(12):                                 # the words of the notes sounding, lit
            lit = max(lit, coloured(0x6A4E10))
            if lit > 40:
                break
            time.sleep(0.15)
        shot("playing")
        assert lit > 40, f"the words of the notes are lit ({lit} pixels)"
        k("\r")
        k('oops = note "zz"')
        k("\x1b[28~", 0.3)
        text = see(["zz"])
        assert any("MUSIC 2" in l for l in text), "the mistake leaves the music playing"
        shot("mistake")
        k("\x1b[29~", 0.3)                                  # Ctrl+.
        text = see(["music stopped"])
        assert not any("MUSIC" in l for l in text), "\n".join(text)
    finally:
        q.close()


def test_code_api(b, opts):
    """bm Code: Ctrl+Q shows the signature and text of the API function under
    the cursor (src/ai/api.tsv), says so when there is none; saving a
    cartridge lints it: an unknown API name is logged and shown."""
    tmp = tempfile.mkdtemp(prefix="bm-codeapi-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [(b("carts/pong.bm"), "bm/pong.bm")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])

    def k(s, gap=0.04):
        # an escape sequence goes in one piece: a lone Esc is one after 6 frames
        for c in re.findall(r"\x1b\[[0-9]*[~A-Z]|\x1bO[A-Z]|.", s, re.S):
            q.send(c)
            time.sleep(gap)

    def expect(needle, timeout=15):
        return q.expect(needle, timeout=timeout).decode(errors="replace")

    def see(words, tries=40):
        text = []
        for _ in range(tries):
            img_ = q.screendump()
            text = screen_text(img_, 6, 12)
            if all(any(w in l for l in text) for w in words):
                return text
            time.sleep(0.25)
        raise AssertionError(f"not on screen: {words}\n" + "\n".join(text))
    try:
        q.boot()
        k("C")
        expect("code: ready")
        # A: the help of the name under the cursor, and the message without one
        k("\x14", 0.3)                                      # Ctrl+T: a new tab
        k("cls(1)")
        k("\x1b[H", 0.1)                                    # Home
        k("\x11", 0.5)                                      # Ctrl+Q
        see(["cls([c])", "fills the screen"])
        k("\x1b", 0.6)                                      # any key closes
        k("\x1b[F\rsprt(1)\x1b[H\x11", 0.1)
        see(["no API help for 'sprt'"])
        # a name with two signatures: the help shows both
        k("\x1b[F\rfset(1, 2)\x1b[H\x11", 0.1)
        see(["fset(n, f, on)", "fset(n, byte)"])
        k("\x1b", 0.6)
        # B: save a cartridge with an unknown name: the lint says so
        k("\x0f", 0.5)                                      # Ctrl+O
        see(["/bm/pong.bm"])
        k("\r")
        expect("code: opened /bm/pong.bm")
        k("\x0c")                                           # Ctrl+L: go to line
        k("1\r", 0.1)
        k("sprt(1, 2, 3)\r")
        k("\x13")                                           # Ctrl+S
        copy_yes(q, "/bm/pong.bm", "/bme/pong.bme")
        expect("code: saved /bme/pong.bme")
        expect("code: lint ")
        expect("sprt:1")
        see(["API:", "sprt"])

        def red_gutter():
            # pixels of the lint colour C_ERR (0xFF6464, 5-6 bit on screen) in the gutter
            w, h, px = img_ = q.screendump()
            return sum(1 for y in range(h) for x in range(0, 30)
                       if pixel(img_, x, y) == (248, 100, 96))
        for _ in range(20):
            if red_gutter():
                break
            time.sleep(0.25)
        assert red_gutter(), "lint: no red line number in the gutter"
        # C: an edit clears the marks
        k("x", 0.5)
        for _ in range(20):
            if not red_gutter():
                break
            time.sleep(0.25)
        assert not red_gutter(), "lint: the gutter mark is still there after an edit"
    finally:
        q.close()
        shutil.rmtree(tmp, ignore_errors=True)


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

    def see_gone(words, gone, tries=40):
        """see(), and none of `gone` on the screen: the first frame after a key can still be the one
        before it (the text is the same), so one sample proves nothing"""
        text = []
        for _ in range(tries):
            text = screen_text(q.screendump(), 6, 12)
            if all(any(w in l for l in text) for w in words) and not any(g in l for l in text for g in gone):
                return text
            time.sleep(0.25)
        raise AssertionError(f"not on screen: {words}, or still there: {gone}\n" + "\n".join(text))

    def coloured(rgb, tol=12):
        """the pixels of about that colour on the screen"""
        w, h, px = q.screendump()
        want = ((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255)
        n = 0
        for i in range(0, len(px), 3):
            if all(abs(px[i + j] - want[j]) <= tol for j in range(3)):
                n += 1
        return n

    def wait_coloured(rgb, tries=40):
        """the count of that colour on the first screen with more than 20 pixels of it, or of the last one.
        One sample is not enough: under load a screendump can catch a frame the guest is still drawing,
        without the colour, though the frames before and after have it (CI misses of this test)"""
        n = 0
        for _ in range(tries):
            n = coloured(rgb)
            if n > 20:
                break
            time.sleep(0.25)
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
        assert wait_coloured(GHOST) > 20, "the suggestion in grey-blue"
        shot("ghost")
        k("\t", 0.4)
        text = see_gone(["  1 function"], ["Tab:"])           # the status line drops "Tab: word" a frame later
        for _ in range(20):                                 # the grey-blue rest is gone once Tab is done
            if coloured(GHOST) < 20:
                break
            time.sleep(0.25)
        pred = wait_coloured(PRED)                          # the colour settles a little after the text
        if pred <= 20:                                      # CI evidence of a miss: build/diag/ is uploaded
            img = q.screendump()
            w, h, px = img
            hist = {}
            for i in range(0, w * 12 * 3, 3):               # the first text row (12 px high)
                key = tuple(px[i + j] >> 4 << 4 for j in range(3))
                hist[key] = hist.get(key, 0) + 1
            top = sorted(hist.items(), key=lambda kv: -kv[1])[:8]
            print("code-completion miss: GHOST=%d PRED=%d" % (coloured(GHOST), pred))
            print("first row colours (rgb>>4<<4: pixels):", top)
            print("screen:\n" + "\n".join(screen_text(img, 6, 12)))
            os.makedirs("build/diag", exist_ok=True)
            _save_png(img, "build/diag/code-complete-miss.png")
        assert pred > 20, "what Tab wrote, green"
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
                     (b("carts/typing.bm"), "bm/typing.bm")])
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
        shutil.rmtree(tmp, ignore_errors=True)


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
    mksd.build(img, [(b("carts/pong.bm"), "bm/pong.bm")])
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
        k("\r")                                             # MYGAME.BME, a project
        see("saved /bme/mygame.bme")
        k("\x1b", 0.5)                                      # back to the code
        k("\x12", 1)                                        # Ctrl+R: try it
        for _ in range(40):                                 # the game is on (slow hosts)
            _, text = settled_screen(q, lambda i, t: True, tries=1)
            if not any("saved /bme/mygame.bme" in l or "line 1/" in l for l in text):
                break
            time.sleep(0.25)
        time.sleep(2)
        k("q")                                              # and back to the SDK
        out = q.expect('bm: "Platform 2D"', timeout=20).decode(errors="replace")
        out += q.expect("tokens", timeout=10).decode(errors="replace")
        assert "stopped with an error" not in out, out
        assert re.search(r"dev kit: Lua peak \d+ KB, data \d+ KB", out), out
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
        saved = subprocess.run(["mtype", "-i", part, "::/BME/MYGAME.BME"], capture_output=True,
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
        see("saved /bme/")
    finally:
        q.close()
    try:
        part = os.path.join(tmp, "part.img")
        with open(img, "rb") as f, open(part, "wb") as o:
            f.seek(2048 * 512)
            o.write(f.read())
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
        names = subprocess.run(["mdir", "-b", "-i", part, "::/BME"], capture_output=True, env=env,
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


def test_sdk_map3d_roundtrip(b, opts):
    """APP-2: a 3D map is saved and reloaded in the bm SDK. A new project,
    T (the 3D template: floor, height, walls, objects), tiles on the floor and
    on the height layer, the markers on the objects layer (Shift+S start,
    Shift+C crate, Shift+O lamp), v (the 3D preview), Ctrl+S. The LAYERS
    section holds the four layers by name and the cells. Then Ctrl+O opens the
    project again: the layers, the floor tile and the start marker are on the
    map page, the 3D preview opens, and a save keeps the cells."""
    tmp = tempfile.mkdtemp(prefix="bm-map3d-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    F3 = "\x1bOR"

    def k(s, gap=0.25):
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
        k("\r", 0.5)                                        # Empty 2D
        see("new project (")
        k(F3, 0.5)                                          # the sprites
        see("SPRITES")
        k(F3, 0.5)                                          # the map
        see("layer 1/1 main")
        k("T")
        see("3D map template ready")
        see("layer 1/4 floor")
        k(" ")                                              # floor tile at (0, 0)
        k("\x1b[C")
        k(" ")                                              # (2, 0)
        k("\x1b[C")
        k(" ")                                              # (4, 0)
        k("l")
        see("map layer 2/4: height")
        k(" ")                                              # height at (4, 0)
        k("l")
        k("l")
        see("map layer 4/4: objects")
        k("O")                                              # lamp at (4, 0)
        see("lamp placed")
        k("\x1b[D")
        k("C")                                              # crate at (2, 0)
        see("crate placed")
        k("\x1b[D")
        k("S")                                              # start at (0, 0)
        see("start placed")
        k("v", 1)
        see("3D preview")
        k("v", 0.5)
        see("2D map editor")
        k("\x13", 1)                                        # Ctrl+S: no name yet, Save as
        see("file name")
        k("\r")
        see("saved /bme/")
        # the project opened again
        k("\x0f", 1)                                        # Ctrl+O
        see("open a cartridge")
        k("\r", 1)
        see("opened /bme/")
        see("4 layers")                                     # the project page: the map has its layers
        k(F3, 0.5)                                          # the map page (the last view of the SDK)
        see("layer 1/4 floor")
        see("(0,0) = ")
        k("l")
        k("l")
        k("l")
        see("map layer 4/4: objects")
        see("(0,0) = 1024")
        k("v", 1)
        see("3D preview")
        k("v", 0.5)
        k("\x13", 1)                                        # a save of the reloaded project
        see("saved /bme/")
    finally:
        q.close()
    try:
        part = os.path.join(tmp, "part.img")
        with open(img, "rb") as f, open(part, "wb") as o:
            f.seek(2048 * 512)
            o.write(f.read())
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
        names = subprocess.run(["mdir", "-b", "-i", part, "::/BME"], capture_output=True, env=env,
                               text=True).stdout.split()
        assert len(names) == 1, names
        data = subprocess.run(["mtype", "-i", part, names[0]], capture_output=True, env=env).stdout
        assert data[:8] == b"BMCART\x00\x00", data[:16]
        secs = {}
        for i in range(data[17]):
            t, off, size, _ = struct.unpack_from("<IIII", data, 128 + i * 16)
            secs[t] = data[off:off + size]
        lay = secs[12]
        w, h, n = struct.unpack_from("<HHH", lay, 0)
        assert (w, h, n) == (256, 256, 4), (w, h, n)
        got = [lay[8 + i * 16:24 + i * 16].rstrip(b"\0") for i in range(4)]
        assert got == [b"floor", b"height", b"walls", b"objects"], got
        base = 8 + 4 * 16

        def cell(layer, x, y):                      # layer 1 is the MAP section, 2.. are in LAYERS
            if layer == 0:
                return struct.unpack_from("<H", secs[3], 4 + (y * w + x) * 2)[0]
            return struct.unpack_from("<H", lay, base + (layer - 1) * w * h * 2 + (y * w + x) * 2)[0]

        def block(layer, x, y):                     # a 16x16 tile is a block of 2x2 cells (tile 0 has an empty corner)
            return [cell(layer, x + i, y + j) for j in (0, 1) for i in (0, 1)]
        for x in (0, 2, 4):
            assert any(block(0, x, 0)), f"floor tile at ({x}, 0)"
        assert any(block(1, 4, 0)), "height at (4, 0)"
        assert not any(block(1, 0, 0)) and not any(block(1, 2, 0)), "height only where it was drawn"
        assert all(cell(2, x, 0) == 0 for x in range(8)), "walls stay empty"
        assert (cell(3, 0, 0), cell(3, 2, 0), cell(3, 4, 0)) == (1024, 1028, 1032), \
            (cell(3, 0, 0), cell(3, 2, 0), cell(3, 4, 0))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def test_sdk_suite(b, opts):
    """The SDK update (2026-10-04): the bm SDK opens Test Kit (Ctrl+O),
    its 3D page lists the models and draws them, i writes the code for one,
    the assistant's model (F6) joins the project and is saved with it; 3 on
    the project page opens bm Studio on the file, whose menu has "Back to
    bm SDK"; the SDK comes back on the project."""
    tmp = tempfile.mkdtemp(prefix="bm-sdk-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [(b("carts/testkit.bm"), "bm/testkit.bm")])
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
        see("Test Kit")
        see("8: ")                                          # the models, on the project page
        shot("testkit")
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
        copy_yes(q, "/bm/testkit.bm", "/bme/testkit.bme")     # a game: its editable copy
        see("saved /bme/testkit.bme")
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
        saved = subprocess.run(["mtype", "-i", part, "::/BME/TESTKIT.BME"], capture_output=True, env=env).stdout
        secs = dict(bmmesh.cart_sections(saved))
        import struct
        assert struct.unpack("<H", secs[bmmesh.SEC_MESH][:2])[0] == 9, "the assistant's model saved"
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def test_sdk_elastic(b, opts):
    """The SDK on the elastic layout (lib.layout, 2026-10-23): the menu's
    Screen row switches it to 1280x720 and back to 640x360 (QEMU has no GPU,
    so it starts at 640x360); at 1280x720 every page fills the screen: the
    tab bar at the top, the hint row and the status bar at the bottom."""
    tmp = tempfile.mkdtemp(prefix="bm-elastic-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [(b("carts/testkit.bm"), "bm/testkit.bm")])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    F1, F2, F3, F4 = "\x1bOP", "\x1bOQ", "\x1bOR", "\x1bOS"
    UP = "\x1b[A"

    def k(s, gap=0.3):
        q.send(s)
        time.sleep(gap)

    def size_is(w, h, tries=40):
        for _ in range(tries):
            got = q.screendump()
            if (got[0], got[1]) == (w, h):
                return got
            time.sleep(0.25)
        raise AssertionError(f"screen {got[0]}x{got[1]}, not {w}x{h}")

    def rows_of(got, y0, y1):
        w, h, px = got
        return screen_text((w, y1 - y0, px[y0 * w * 3:y1 * w * 3]))

    def shot(got, name):
        if opts.shots:
            _save_png(got, os.path.join(opts.shots, f"elastic-{name}.png"))
    try:
        q.boot()
        k("e", 1)
        k("\x0f", 0.5)                                     # Ctrl+O: the cartridges
        k("\r", 1)
        small = size_is(640, 360)
        shot(small, "640-project")
        k("\x1b", 0.5)                                     # the menu: Screen is the row before Exit
        k(UP, 0.3)
        k(UP, 0.3)
        k("\r", 1)
        big = size_is(1280, 720)
        time.sleep(1)
        big = q.screendump()
        top = rows_of(big, 0, 16)[0]
        assert "project" in top and "testkit.bm" in top, top
        bottom = rows_of(big, 720 - 40, 720)
        assert any(l.strip() for l in bottom), "the hint row and the status bar are at the bottom of 720"
        shot(big, "720-menu")
        k("\x1b", 0.5)                                     # back to the page
        for name, key in (("project", F1), ("code", F2), ("sprite", F3), ("map", F3), ("3d", F4)):
            k(key, 1)
            for _ in range(20):                             # a frame at 720p is long: not a half drawn one
                got = q.screendump()
                if (got[0], got[1]) == (1280, 720) and any(l.strip() for l in rows_of(got, 720 - 40, 720)):
                    break
                time.sleep(0.4)
            shot(got, "720-" + name)
            assert (got[0], got[1]) == (1280, 720), f"{name}: {got[0]}x{got[1]}"
            assert any(l.strip() for l in rows_of(got, 720 - 40, 720)), f"{name}: the bottom bars"
        k("\x1b", 0.5)                                     # the menu keeps its row: Screen again, back to 640x360
        k("\r", 1)
        size_is(640, 360)
        q.send("\x1c")                                     # Ctrl+\ (Ctrl+Esc): leave
        time.sleep(1)
        q.send("\x1c")
        q.expect("> ", timeout=20)
    finally:
        q.close()
        shutil.rmtree(tmp, ignore_errors=True)


SUITE_TOOLS = (("1", "code", ("\x1bOP", "\x1bOQ", "\x1bOR")), ("2", "pixel", ("\x1bOP", "\x1bOQ", "\x1bOR", "\x1bOS")),
               ("3", "studio", ("\x1bOP", "\x1bOQ")), ("4", "animator", ("\x1bOP", "\x1bOQ", "\x1bOR", "\x1bOS")),
               ("5", "mesh", ("\x1bOP", "\x1bOQ", "\x1bOR")), ("6", "sound", ("\x1bOP", "\x1bOQ", "\x1bOR", "\x1bOS")))


def test_suite_elastic(b, opts):
    """Each tool of the suite (bm Code, Pixel, Studio, Animator, Mesh, Sound)
    opened from the SDK with bm_screen=1280x720 in bm/config.txt: the screen
    is 1280x720 and its pages are drawn (screenshots with --shots). -k
    suite_elastic=NAME runs one tool."""
    only = os.environ.get("BM_SUITE_TOOL", "")
    for key, name, pages in SUITE_TOOLS:
        if only and only != name:
            continue
        tmp = tempfile.mkdtemp(prefix="bm-suite-")
        img = os.path.join(tmp, "sd.img")
        cfg = os.path.join(tmp, "config.txt")
        with open(cfg, "w") as f:
            f.write("layout=us\nbm_screen=1280x720\n")
        mksd.build(img, [(cfg, "bm/config.txt"), (b("carts/testkit.bm"), "bm/testkit.bm")])
        q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])

        def k(s, gap=0.4):
            q.send(s)
            time.sleep(gap)

        def grab(label):
            got = q.screendump()
            for _ in range(20):
                got = q.screendump()
                if (got[0], got[1]) == (1280, 720):
                    break
                time.sleep(0.4)
            time.sleep(0.6)
            got = q.screendump()
            if opts.shots:
                _save_png(got, os.path.join(opts.shots, f"suite-{name}-{label}.png"))
            assert (got[0], got[1]) == (1280, 720), f"{name} {label}: {got[0]}x{got[1]}"
        try:
            q.boot()
            k("e", 1)
            k("\x0f", 0.5)
            k("\r", 1.5)
            k(key, 3)
            grab("open")
            for i, fk in enumerate(pages):
                k(fk, 1.5)
                grab(f"f{i + 1}")
            k("\x1b", 1.5)
            grab("menu")
            q.send("\x1c")
            time.sleep(1)
            q.send("\x1c")
        finally:
            q.close()
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
    mksd.build(img, [(b("carts/nano8.bm"), "bm/nano8.bm"), (api, "carts/nano8/api.p8"),
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


def test_sdk_template_pi(b, opts):
    """The last template (3D with models) in the Pi's bm SDK: Ctrl+N, the last of eight templates (one Up
    from the first row), a name with Ctrl+S, Ctrl+B builds the game next to
    the project; the .BM carries the template's Lua."""
    tmp = tempfile.mkdtemp(prefix="bm-sdsp-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])

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
        see("3D with models")                               # the eighth, in the list
        k("\x1b[A", 0.3)                                    # one Up wraps to the last row
        k("\r", 0.5)
        see("new project (3D with models)")
        k("\x13", 1)                                        # Ctrl+S: no name yet, Save as
        see("file name")
        k("\r")
        see("saved /bme/mygame.bme")
        k("\x02", 1)                                        # Ctrl+B: the game next to it
        see("built ")
    finally:
        q.close()
    try:
        part = os.path.join(tmp, "part.img")
        with open(img, "rb") as f, open(part, "wb") as o:
            f.seek(2048 * 512)
            o.write(f.read())
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
        names = subprocess.run(["mdir", "-b", "-i", part, "::/BM"], capture_output=True, env=env,
                               text=True).stdout.upper()
        projects = subprocess.run(["mdir", "-b", "-i", part, "::/BME"], capture_output=True, env=env,
                                  text=True).stdout.upper()
        assert "MYGAME.BME" in projects and "MYGAME.BME" not in names, (projects, names)   # the project is in /bme
        assert "MYGAME.BM" in names, names                                                  # the game in the games folder
        game = subprocess.run(["mtype", "-i", part, "::/BM/MYGAME.BM"], capture_output=True,
                              env=env).stdout
        assert game[:8] == b"BMCART\x00\x00", game[:16]
        assert b"list = models()" in game, "the template's Lua in the .BM"
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def test_sdk_code_pi(b, opts):
    """The code page of the Pi's bm SDK (as the RGB30's test_sdk_pad_code): a
    new Empty 2D project, F2 is the code, 'do end ' is typed at the cursor,
    Ctrl+S saves the project; its LUA section has the typed text."""
    tmp = tempfile.mkdtemp(prefix="bm-sdcp-")
    img = os.path.join(tmp, "sd.img")
    mksd.build(img, [])
    q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    F2 = "\x1bOQ"

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
        k("\r", 0.5)                                        # the first row: Empty 2D
        see("new project (Empty 2D)")
        k(F2, 0.5)                                          # the code
        see("line 1/")
        for ch in "do end ":
            k(ch, 0.2)
        k("\x13", 1)                                        # Ctrl+S: no name yet, Save as
        see("file name")
        k("\r")
        see("saved /bme/")
    finally:
        q.close()
    try:
        part = os.path.join(tmp, "part.img")
        with open(img, "rb") as f, open(part, "wb") as o:
            f.seek(2048 * 512)
            o.write(f.read())
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
        names = subprocess.run(["mdir", "-b", "-i", part, "::/BME"], capture_output=True, env=env,
                               text=True).stdout.split()
        assert names, "no project saved"
        data = subprocess.run(["mtype", "-i", part, names[0]], capture_output=True, env=env).stdout
        assert data[:8] == b"BMCART\x00\x00", data[:16]
        secs = {}
        for i in range(data[17]):
            t, off, size, _ = struct.unpack_from("<IIII", data, 128 + i * 16)
            secs[t] = data[off:off + size]
        assert b"do end " in secs[1], secs[1][:120]
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


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
