#!/usr/bin/env python3
"""qemurec: records the console in QEMU (the kernel of the Pi, -M raspi0) at 60 fps of
the GUEST's time, whatever the speed of the host.

Why: on a cloud host QEMU in real time does ~39 fps in 3D (docs/video/PIANO.md §6). With
`-icount shift=1,sleep=off` the guest clock follows the instructions (2 ns each), so the
guest makes 60 fps and loses none; the host is ~3x slower than real time. We then

  * read the guest's clock (the BCM2835 system timer, `xp` of the monitor) all the time;
  * send every key at its guest time (the script is in bmhost's format: "FRAME type CODES",
    FRAME in 1/60 s of the recording);
  * grab the screen (Xvfb + QEMU gtk + ffmpeg x11grab, as video/titan-clash/record_qemu.py),
    stamp every grabbed picture with the guest time and write one picture for each
    1/60 s of guest time: console.mkv (lossless, 640x360, 60 fps);
  * assemble(): the 1920x1080 page of video/lib/bmvideo.py (or overlay3d.py) in an mp4.

Same script -> same video (to the frame, give or take a frame at a key).

  from qemurec import Recorder
  r = Recorder("out", kernel="build/kernel.img")
  r.boot(sd_files=[("build/x.bm", "carts/x.bm")], config={"layout": "us"})
  r.record(input_lines, frames=3600, out="out/console.mkv")
  r.close()

Needs qemu-system-arm, Xvfb, ffmpeg, mkfs.vfat and mtools (scripts/mksd.py).
"""
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import threading
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
sys.path.insert(0, os.path.join(ROOT, "tests"))

FPS = 60
US_PER_FRAME = 1_000_000 / FPS
W, H = 640, 360                       # the console's picture
TIMER = 0x20003004                    # BCM2835 system timer CLO, CHI (1 MHz)

# ------------------------------------------------------------------ keys
# the codes of bmhost's `type` (keyp(), see bmvideo.KEYS) -> QEMU qcodes of a USB keyboard
SPECIAL = {
    "\xf0": "up", "\xf1": "down", "\xf2": "left", "\xf3": "right", "\xf4": "home", "\xf5": "end",
    "\xf6": "pgup", "\xf7": "pgdn", "\xf8": "delete",
    "\xf9": "f1", "\xfa": "f2", "\xfb": "f3", "\xfc": "f4", "\xfd": "f5",
    "\x1b": "esc", "\n": "ret", "\r": "ret", "\t": "tab", " ": "spc", "\x7f": "backspace", "\x08": "backspace",
}
ESCSEQ = {"[17~": "f6", "[18~": "f7", "[19~": "f8", "[20~": "f9", "[21~": "f10",
          "[28~": ("ctrl", "ret"), "[29~": ("ctrl", "dot")}
PUNCT = {  # US layout (config layout=us): char -> (shift, qcode)
    "`": (0, "grave_accent"), "~": (1, "grave_accent"), "-": (0, "minus"), "_": (1, "minus"), "=": (0, "equal"),
    "+": (1, "equal"), "[": (0, "bracket_left"), "{": (1, "bracket_left"), "]": (0, "bracket_right"),
    "}": (1, "bracket_right"), "\\": (0, "backslash"), "|": (1, "backslash"), ";": (0, "semicolon"),
    ":": (1, "semicolon"), "'": (0, "apostrophe"), '"': (1, "apostrophe"), ",": (0, "comma"), "<": (1, "comma"),
    ".": (0, "dot"), ">": (1, "dot"), "/": (0, "slash"), "?": (1, "slash"),
    "!": (1, "1"), "@": (1, "2"), "#": (1, "3"), "$": (1, "4"), "%": (1, "5"), "^": (1, "6"), "&": (1, "7"),
    "*": (1, "8"), "(": (1, "9"), ")": (1, "0"),
}


def decode(codes):
    """The text of a `type` line (with \\xNN escapes) -> a list of key presses, each a list of
    qcodes held together: [["ctrl", "s"], ["up"], ["shift", "a"]]."""
    s = bytes(codes, "latin-1").decode("unicode_escape").encode("latin-1") if "\\" in codes else codes.encode("latin-1")
    s = s.decode("latin-1")
    out, i, shift = [], 0, False
    while i < len(s):
        c = s[i]
        if c == "\xee":                              # Ctrl+Shift+letter: 0xEE then the code 1..26
            i += 1
            out.append(["ctrl", "shift", chr(96 + ord(s[i]))])
        elif c == "\x1b" and s[i + 1:i + 5] in ESCSEQ:
            q = ESCSEQ[s[i + 1:i + 5]]
            out.append(list(q) if isinstance(q, tuple) else [q])
            i += 4
        elif c in SPECIAL:
            out.append([SPECIAL[c]])
        elif 1 <= ord(c) <= 26:
            out.append(["ctrl", chr(96 + ord(c))])
        elif c.isalpha() and c.isascii():
            out.append((["shift"] if c.isupper() else []) + [c.lower()])
        elif c.isdigit():
            out.append([c])
        elif c in PUNCT:
            sh, q = PUNCT[c]
            out.append((["shift"] if sh else []) + [q])
        else:
            raise ValueError("key? %r" % c)
        i += 1
    return out


SERIAL_KEY = {"\xf0": "\x1b[A", "\xf1": "\x1b[B", "\xf2": "\x1b[D", "\xf3": "\x1b[C", "\xf4": "\x1b[H", "\xf5": "\x1b[F",
              "\xf6": "\x1b[5~", "\xf7": "\x1b[6~", "\xf8": "\x1b[3~", "\xf9": "\x1bOP", "\xfa": "\x1bOQ", "\xfb": "\x1bOR",
              "\xfc": "\x1bOS", "\xfd": "\x1b[15~"}


def serial_bytes(codes):
    """The text of a `type` line as the kernel's serial terminal wants it (src/bm/runtime.c serial_text): the same key codes
    as bmhost's typed keys, arrows and F-keys as ANSI sequences, Esc alone followed by a newline (which the terminal
    ignores) so that it is not held back waiting for the next byte. None for what the serial line cannot say (Ctrl+Shift)."""
    s = bytes(codes, "latin-1").decode("unicode_escape").encode("latin-1").decode("latin-1") if "\\" in codes else codes
    if "\xee" in s:
        return None
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c == "\x1b":
            m = next((q for q in ("[17~", "[18~", "[19~", "[20~", "[21~", "[28~", "[29~") if s.startswith(q, i + 1)), None)
            if m:
                out.append("\x1b" + m)
                i += 1 + len(m)
                continue
            out.append("\x1b\n")
        elif c in SERIAL_KEY:
            out.append(SERIAL_KEY[c])
        elif c == "\n":
            out.append("\r")
        else:
            out.append(c)
        i += 1
    return "".join(out).encode("latin-1")


def parse_input(path_or_lines):
    """bmhost's input file -> [(frame, kind, args)] in order. Kinds: type, pad, shot (the others are ignored)."""
    lines = open(path_or_lines).read().splitlines() if isinstance(path_or_lines, str) else path_or_lines
    ev = []
    for ln in lines:
        ln = ln.strip()
        if not ln or ln.startswith("#"):
            continue
        f, kind, args = (ln.split(None, 2) + [""])[:3]
        ev.append((int(f), kind, args))
    return ev


class Qmp:
    """One connection to QEMU's QMP socket (a lock: the sampler and the keys share it)."""
    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX)
        self.sock.connect(path)
        self.f = self.sock.makefile("rw")
        self.f.readline()
        self.lock = threading.Lock()
        self.cmd({"execute": "qmp_capabilities"})

    def cmd(self, c):
        with self.lock:
            self.f.write(json.dumps(c) + "\n")
            self.f.flush()
            while True:
                r = json.loads(self.f.readline())
                if "return" in r or "error" in r:
                    assert "error" not in r, r
                    return r


# ---------------------------------------------------------------- the recorder
class Recorder:
    def __init__(self, workdir, kernel=None, shift=1, display=":99", extra=()):
        self.work = os.path.abspath(workdir)
        os.makedirs(self.work, exist_ok=True)
        self.kernel = kernel or os.path.join(ROOT, "build", "kernel.img")
        self.shift = shift
        self.display = display
        self.extra = list(extra)
        self.kbd = True
        self.disp = "gtk,show-menubar=off,zoom-to-fit=off"
        self.xvfb = self.qemu = None
        self.log = b""

    # ---------------------------------------------------------- start / stop
    def boot(self, sd_files=(), config=None, timeout=240):
        """A clean SD card with the given files (list of (source, path on the card)) and bm/config.txt
        (layout=us by default: the keys below are US), then QEMU up to the cartridge menu."""
        import mksd
        import qemu_test as qt
        cfg = {"layout": "us"}
        cfg.update(config or {})
        cfgfile = os.path.join(self.work, "config.txt")
        with open(cfgfile, "w") as f:
            f.write("".join("%s=%s\n" % kv for kv in cfg.items()))
        img = os.path.join(self.work, "sd.img")
        if os.path.exists(img):
            os.remove(img)
        mksd.build(img, [(cfgfile, "bm/config.txt")] + list(sd_files))
        os.environ["DISPLAY"] = self.display
        self.xvfb = subprocess.Popen(["Xvfb", self.display, "-screen", "0", "%dx%dx24" % (1920, 1080)],
                                     stderr=subprocess.DEVNULL)
        time.sleep(1.0)
        mon, qmp = (os.path.join(self.work, n) for n in ("mon.sock", "qmp.sock"))
        for p in (mon, qmp):
            if os.path.exists(p):
                os.remove(p)
        tcp = qt.free_port()
        self.qemu = subprocess.Popen(
            ["qemu-system-arm", "-M", "raspi0", "-bios", self.kernel, "-drive", f"if=sd,format=raw,file={img}",
             "-icount", f"shift={self.shift},sleep=off", *(["-device", "usb-kbd,port=1"] if self.kbd else []),
             "-display", self.disp,
             "-serial", f"tcp:127.0.0.1:{tcp},server=on,wait=on", "-serial", "null",
             "-monitor", f"unix:{mon},server=on,wait=off", "-qmp", f"unix:{qmp},server=on,wait=off", *self.extra],
            stderr=subprocess.DEVNULL)
        self.port = qt.bm_load.Port(f"tcp:127.0.0.1:{tcp}", 115200)
        self.expect("cartridge menu", timeout)
        for _ in range(100):
            if os.path.exists(mon):
                break
            time.sleep(0.1)
        self.qmp_path = qmp
        self.q_in = self.q_clk = Qmp(qmp)      # QMP takes one client; a lock shares it

    def expect(self, needle, timeout=30):
        needle = needle.encode() if isinstance(needle, str) else needle
        end = time.time() + timeout
        while needle not in self.log:
            if time.time() > end:
                raise TimeoutError("waiting for %r; serial: %s" % (needle, self.log[-600:].decode(errors="replace")))
            self.log += self.port.read(0.1)
        i = self.log.index(needle) + len(needle)
        seen, self.log = self.log[:i], self.log[i:]
        return seen

    def close(self):
        for p in (self.qemu, self.xvfb):
            if p:
                p.kill()
                p.wait()
        self.qemu = self.xvfb = None

    # ------------------------------------------------------- guest clock
    def vtime_us(self):
        """The guest's clock in microseconds (64 bits of the system timer, read with the monitor's xp)."""
        out = self.q_clk.cmd({"execute": "human-monitor-command",
                              "arguments": {"command-line": "xp /2wx 0x%x" % TIMER}})["return"]
        m = re.search(r":\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)", out)
        return int(m[1], 16) | (int(m[2], 16) << 32)

    # ------------------------------------------------------------ input
    def _qmp(self, cmd):
        return self.q_in.cmd(cmd)

    def _events(self, qcodes, down):
        return [{"type": "key", "data": {"down": down, "key": {"type": "qcode", "data": q}}} for q in qcodes]

    def press(self, qcodes):
        """A chord: the keys down together, then up (one QMP call each, a frame or two apart in the guest)."""
        self._qmp({"execute": "input-send-event", "arguments": {"events": self._events(qcodes, True)}})
        self._qmp({"execute": "input-send-event", "arguments": {"events": self._events(reversed(qcodes), False)}})

    def hold(self, qcodes, down):
        self._qmp({"execute": "input-send-event", "arguments": {"events": self._events(qcodes, down)}})

    # -------------------------------------------------------- the recording
    def record(self, events, frames, out, hold_frames=3, grab_size=(W, H), t0_us=None, serial=False):
        """Plays `events` (parse_input) from now for `frames` frames of guest time; console.mkv gets
        exactly `frames` pictures at 60 fps. Returns the list of (frame, name) of the `shot` lines (PNG next
        to `out`). A key stays down `hold_frames` frames (3 = 50 ms, as in bmhost)."""
        os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
        gw, gh = grab_size
        t0 = self.vtime_us() if t0_us is None else t0_us
        samples = []                       # (wall, guest µs), appended by the sampler
        stop = threading.Event()

        def sampler():
            while not stop.is_set():
                a = time.time()
                v = self.vtime_us()
                samples.append(((a + time.time()) / 2, v))
                time.sleep(0.015)
        th = threading.Thread(target=sampler, daemon=True)
        th.start()
        while not samples:
            time.sleep(0.01)

        grab = subprocess.Popen(
            ["ffmpeg", "-hide_banner", "-loglevel", "error", "-f", "x11grab", "-draw_mouse", "0", "-framerate", "60",
             "-video_size", f"{gw}x{gh}", "-i", f"{self.display}.0+0,0", "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
            stdout=subprocess.PIPE, stdin=subprocess.DEVNULL)
        enc = subprocess.Popen(
            ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24",
             "-s", f"{gw}x{gh}", "-r", str(FPS), "-i", "-", "-c:v", "ffv1", "-level", "3", "-g", "1", out],
            stdin=subprocess.PIPE)
        size = gw * gh * 3
        pending, state = [], {"i": 0, "last": None, "stat": [0, 0]}

        def v_at(w):
            """guest µs at wall time w (linear between the two samples around it)."""
            lo, hi = 0, len(samples) - 1
            while lo < hi:
                mid = (lo + hi) // 2
                if samples[mid][0] < w:
                    lo = mid + 1
                else:
                    hi = mid
            if lo == 0:
                return samples[0][1]
            (w0, v0), (w1, v1) = samples[lo - 1], samples[lo]
            return v0 + (v1 - v0) * (w - w0) / max(w1 - w0, 1e-9)

        def emit_until(v_us, frame):
            """pictures for every 1/60 s of guest time before v_us: the last grabbed picture stays until then."""
            while state["i"] < frames and t0 + state["i"] * US_PER_FRAME < v_us:
                pic = state["last"] if state["last"] is not None else frame
                enc.stdin.write(pic)
                state["i"] += 1

        def pump():
            while state["i"] < frames:
                buf = grab.stdout.read(size)
                if len(buf) < size:
                    break
                w = time.time()
                pending.append((w, buf))
                while pending and (pending[0][0] <= samples[-1][0]):
                    w0, b0 = pending.pop(0)
                    v = max(v_at(w0), t0)
                    emit_until(v, b0)
                    state["last"] = b0
        pt = threading.Thread(target=pump, daemon=True)
        pt.start()

        # every key is a pair of operations (down at its frame, up `hold_frames` later, or sooner if the same key comes
        # again); they are done in time order, so keys 1-2 frames apart do not queue behind each other's hold.
        ops, shots = [], []                         # (frame, order, qcodes, down?)
        for f, kind, args in (e for e in events if e[0] < frames):
            chords = []
            if kind == "type" and serial and serial_bytes(args) is not None:
                ops.append((f, 1, serial_bytes(args), True, -2))      # bytes for the serial line: no key-up to wait for
            elif kind == "type":
                chords = decode(args)
            elif kind == "chord":                   # "ctrl+esc": qcodes joined by +
                chords = [args.split("+")]
            elif kind == "keydown":
                ops.append((f, 1, [args], True, -1))
            elif kind == "keyup":
                ops.append((f, 0, [args], False, -1))
            elif kind == "shot":
                shots.append((f, args))
            for i, chord in enumerate(chords):      # the characters of one line come one after the other
                at = f + i * hold_frames
                pid = len(ops)
                ops.append((at, 1, chord, True, pid))
                ops.append((at + hold_frames, 0, chord, False, pid))
        ops.sort(key=lambda o: (o[0], o[1]))
        down, deadline_bad = {}, 0                  # qcode -> id of the press that holds it
        for f, _, chord, is_down, pid in ops:
            if pid == -2:                           # serial bytes
                target = t0 + f * US_PER_FRAME
                while self.vtime_us() < target:
                    time.sleep(0.002)
                if self.vtime_us() - target > 2 * US_PER_FRAME:
                    deadline_bad += 1
                self.port.write(chord)
                continue
            target = t0 + f * US_PER_FRAME
            while self.vtime_us() < target:
                time.sleep(0.002)
            if self.vtime_us() - target > 2 * US_PER_FRAME:
                deadline_bad += 1
            if is_down:
                again = [q for q in chord if q in down]
                if again:                           # the key is still down from the last press: let it go first
                    self.hold(again, False)
                self.hold(chord, True)
                down.update({q: pid for q in chord})
            else:
                mine = [q for q in reversed(chord) if q in down and (pid < 0 or down[q] == pid)]
                if mine:
                    self.hold(mine, False)
                for q in mine:
                    del down[q]
        # until the last frame
        end = t0 + frames * US_PER_FRAME
        while self.vtime_us() < end + 2 * US_PER_FRAME:
            time.sleep(0.01)
        pt.join(timeout=30)
        stop.set()
        grab.kill()
        if state["last"] is not None:
            while state["i"] < frames:
                enc.stdin.write(state["last"])
                state["i"] += 1
        enc.stdin.close()
        enc.wait()
        self.stats = {"frames": state["i"], "late_keys": deadline_bad, "samples": len(samples),
                      "wall_s": samples[-1][0] - samples[0][0],
                      "guest_s": (samples[-1][1] - samples[0][1]) / 1e6}
        return shots

    def screenshot(self, path, size=(W, H)):
        subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "x11grab", "-draw_mouse", "0",
                        "-video_size", "%dx%d" % size, "-i", f"{self.display}.0+0,0", "-frames:v", "1", path],
                       check=True)


# ----------------------------------------------------------------- the page
def assemble(console_mkv, ass_path, out_mp4, audio=None, seconds=None, audio_delay=0.0):
    """The 1920x1080 page of bmvideo (console 2x, side panel, overlay) from the lossless recording."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import bmvideo
    cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", console_mkv]
    if audio:
        cmd += ["-itsoffset", "%.3f" % audio_delay, "-i", audio]       # the sound of the same keys, from bmhost --wav
    cmd += ["-vf", bmvideo.filter_chain(ass_path), "-r", "60", "-c:v", "libx264", "-preset", "medium", "-crf", "19",
            "-pix_fmt", "yuv420p"]
    if audio:
        cmd += ["-af", "apad", "-c:a", "aac", "-b:a", "160k", "-shortest"]
    if seconds:
        cmd += ["-t", str(seconds)]
    cmd += ["-movflags", "+faststart", out_mp4]
    return subprocess.run(cmd, check=True)


def write_srt(s, path):
    """The narrator's lines of a bmvideo.Script as .srt (YouTube subtitles, English)."""
    def t(frame):
        ms = round(frame * 1000 / FPS)
        return "%02d:%02d:%02d,%03d" % (ms // 3600000, ms // 60000 % 60, ms // 1000 % 60, ms % 1000)
    with open(path, "w", encoding="utf-8") as f:
        for n, (a, b, text) in enumerate(s.lines_say, 1):
            f.write("%d\n%s --> %s\n%s\n\n" % (n, t(a), t(b if b is not None else a + 120), text))


def extract_sd(img, dest):
    """The files on the card after the recording (what the tool saved): every file under the card's root into dest/."""
    part = img + ".part"
    with open(img, "rb") as f, open(part, "wb") as o:
        f.seek(2048 * 512)
        shutil.copyfileobj(f, o)
    shutil.rmtree(dest, ignore_errors=True)
    os.makedirs(dest)
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    subprocess.run(["mcopy", "-s", "-n", "-i", part, "::/", dest], check=True, env=env)
    for name in os.listdir(dest):                       # FAT names come back in capitals: carts/, TROPIC.BME as the tools wrote them
        if name != name.lower() and os.path.isdir(os.path.join(dest, name)):
            os.rename(os.path.join(dest, name), os.path.join(dest, name.lower()))
    os.remove(part)
    return dest
