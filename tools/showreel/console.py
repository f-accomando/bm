#!/usr/bin/env python3
"""
The showreel, on the console in QEMU: the villager of Studio Village (its
model, skeleton and walk, carts/village/models.bm) in a new cartridge whose
code is one line. In the SDK a small map is painted; in bm Code the
assistant writes the movement with the arrows and the rest of the game is
typed (the map becomes the 3D ground, the villager walks on it); then the
game is played.

  python3 tools/showreel/console.py BUILD OUTDIR        the scenes
  python3 tools/showreel/console.py BUILD OUTDIR --test  the game only

BUILD is the build directory (kernel.img). OUTDIR gets frames/NNNNNN.png
(QEMU screendumps, about 12 a second), frames.json (the time of each frame
and the scene marks) and the cartridge as saved.
"""
import json
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import zlib

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import qemu_test as qt  # noqa: E402  (Qemu, read_ppm, screen_text; mksd and mkbm)

mksd, mkbm = qt.mksd, qt.mkbm

# ------------------------------------------------------------------ the map
# 8x8 cells of the starter sheet of bm Studio (a 16 px tile is 4 cells):
# grass, sand, water, earth. 0 is an empty cell.
GRASS, SAND, WATER, EARTH = 1, 24, 22, 2
W, H = 24, 14
PATH_ROWS = (9, 10)
POND = (15, 2, 19, 5)                   # x0, y0, x1, y1
FIELD = (3, 2, 7, 4)


def planned_map():
    """The map the SDK scene paints, for --test."""
    cells = [GRASS] * (256 * 256)
    put = lambda x, y, n: cells.__setitem__(y * 256 + x, n)
    for y in PATH_ROWS:
        for x in range(W):
            put(x, y, SAND)
    for y in range(POND[1], POND[3] + 1):
        for x in range(POND[0], POND[2] + 1):
            put(x, y, WATER)
    for y in range(FIELD[1], FIELD[3] + 1):
        for x in range(FIELD[0], FIELD[2] + 1):
            put(x, y, EARTH)
    return 256, 256, struct.pack("<%dH" % len(cells), *cells)      # 16-bit cells


# ------------------------------------------------------------------ the code
# what the assistant inserts (src/ai/kb/howto_move.txt), then edited
CODE_TOP = "local x, y, speed = 12, 10, 0.1"
CODE_VARS = """local W, H = 24, 14
local man, ground
local px, py, t, ry = x, y, 0, 0
"""
CODE_INIT = """function _init()
man = model("villager")
local v, f, uv = {}, {}, {}
for my = 0, H - 1 do
for mx = 0, W - 1 do
local n, k = mget(mx, my), #v // 3
local a, b = mx / 2, (H - my) / 2
local u, w = n % 32 * 8 + .2, n // 32 * 8 + .2
for _, c in ipairs({a, 0, b - .5, a, 0, b, a + .5, 0, b, a + .5, 0, b - .5}) do
v[#v + 1] = c
end
for _, c in ipairs({k + 1, k + 2, k + 3, -1, k + 1, k + 3, k + 4, -1}) do
f[#f + 1] = c
end
for _, c in ipairs({u, w + 7.6, u, w, u + 7.6, w, u, w + 7.6, u + 7.6, w, u + 7.6, w + 7.6}) do
uv[#uv + 1] = c
end
end
end
ground = mesh(v, f, uv)
end
"""
CODE_DRAW = """function _draw()
local moving = x ~= px or y ~= py
if moving then
ry = math.atan(px - x, y - py)
t = t + 1 / 60
end
px, py = x, y
cls(0x8CC8F0)
zclear()
local wx, wz = x / 2, (H - y) / 2
camera3d(wx, 3.2, wz - 5.5, 0, -0.5, 60)
light3d(-0.4, 0.8, -0.5, 0.45)
draw3d(ground, 0, 0, 0, 0, 0, 0, 1, 1)
if moving then animate(man, "walk", t) else animate(man) end
draw3d(man, wx, 0, wz, 0, ry, 0, 0.22)
end
"""
ASSIST_MOVE = """local x, y, speed = 320, 180, 2

function _update()
  if btn(0) then x = x - speed end   -- sinistra
  if btn(1) then x = x + speed end   -- destra
  if btn(2) then y = y - speed end   -- su
  if btn(3) then y = y + speed end   -- giu
end
"""


def indent(code):
    """The code as bm Code shows it (it indents by itself while typing)."""
    out, depth = [], 0
    for line in code.splitlines():
        s = line.strip()
        if re.match(r"^(end|else|elseif|until|\})\b", s):
            depth -= 1
        out.append("  " * depth + s if s else "")
        if re.search(r"(\bdo|\bthen|^function\b.*\)|\bfunction\b.*\))\s*$", s) or s == "else":
            depth += 1
    return "\n".join(out) + "\n"


def final_lua():
    """The whole code of the game, as it is at the end of the bm Code scene."""
    move = ASSIST_MOVE.replace("local x, y, speed = 320, 180, 2", CODE_TOP)
    return ("-- My Village: a villager on a map\n\n" + move + "\n" + CODE_VARS + "\n"
            + indent(CODE_INIT) + "\n" + indent(CODE_DRAW))


# ------------------------------------------------------------------ .bm files
def sections(data):
    n = data[17]                        # header: "<HHHHBBHI" from byte 8
    out = []
    for i in range(n):
        typ, off, length, _ = struct.unpack_from("<IIII", data, 128 + 16 * i)
        out.append((typ, data[off:off + length]))
    return out


def start_cart():
    """The cartridge the scenes start from, "My Village": the models,
    skeleton, animations and sheet of carts/village/models.bm and one line
    of code: the game is written on the console."""
    data = open(os.path.join(ROOT, "carts", "village", "models.bm"), "rb").read()
    extra = [(t, b) for t, b in sections(data) if t != mkbm.SEC_LUA]
    return mkbm.pack(b"-- My Village: a villager on a map\n", title="My Village", author="bm",
                     res=struct.unpack_from("<HH", data, 12), extra=extra)


def rebuild(data, lua=None, map_=None):
    """The cartridge with another code and map (the rest as it is)."""
    res = struct.unpack_from("<HH", data, 12)
    title = data[24:72].split(b"\0")[0].decode()
    author = data[72:104].split(b"\0")[0].decode()
    extra = []
    for typ, body in sections(data):
        if typ == mkbm.SEC_LUA:
            if lua is None:
                lua = body
            continue
        if typ == mkbm.SEC_MAP and map_ is not None:
            continue
        extra.append((typ, body))
    if map_ is not None:
        w, h, cells = map_
        extra.append((mkbm.SEC_MAP, struct.pack("<HH", w, h) + cells))
    lua = lua.encode() if isinstance(lua, str) else lua
    return mkbm.pack(lua, title=title, author=author, res=res, extra=extra)


# ------------------------------------------------------------------ capture
def write_png(path, w, h, rgb):
    raw = b"".join(b"\0" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 3)) + chunk(b"IEND", b""))


class Capture(threading.Thread):
    """Screendumps through one monitor connection, as fast as `fps`."""

    def __init__(self, q, outdir, fps=12):
        super().__init__(daemon=True)
        self.q, self.dir, self.fps = q, outdir, fps
        self.frames, self.marks = [], []
        self.latest = (0.0, None)          # (time, the last screen), for Console.screen
        self.running = True
        self.sock = socket.socket(socket.AF_UNIX)
        self.sock.connect(q.mon_path)
        self.buf = b""
        self.prompt()

    def prompt(self):
        while b"(qemu) " not in self.buf:
            self.buf += self.sock.recv(65536)
        self.buf = self.buf[self.buf.rindex(b"(qemu) ") + 7:]

    def mark(self, name):
        self.marks.append({"name": name, "t": time.time()})
        print("scene: " + name, flush=True)

    def run(self):
        ppm = os.path.join(self.q.tmp, "cap.ppm")
        while self.running:
            t0 = time.time()
            self.sock.sendall(f"screendump {ppm}\n".encode())
            self.prompt()
            try:
                w, h, rgb = qt.read_ppm(ppm)
            except (OSError, ValueError, IndexError):
                continue
            f = "%06d.png" % len(self.frames)
            self.latest = (t0, (w, h, rgb[:w * h * 3]))
            write_png(os.path.join(self.dir, f), w, h, rgb[:w * h * 3])
            self.frames.append({"f": f, "t": t0, "w": w, "h": h})
            time.sleep(max(0.0, 1 / self.fps - (time.time() - t0)))

    def stop(self, path):
        self.running = False
        self.join()
        self.sock.close()
        with open(path, "w") as f:
            json.dump({"frames": self.frames, "marks": self.marks, "end": time.time()}, f)


# ------------------------------------------------------------------ helpers
SEQ = re.compile(r"\x1b\[[0-9]*[~A-Z]|\x1bO[A-Z]|.", re.S)
UP, DOWN, RIGHT, LEFT = "\x1b[A", "\x1b[B", "\x1b[C", "\x1b[D"
HOME, END, F3, F5, F6 = "\x1b[H", "\x1b[F", "\x1bOR", "\x1b[15~", "\x1b[17~"
TAB, ENTER, BS = "\t", "\r", "\x7f"
CTRL = lambda c: chr(ord(c.lower()) - 96)


class Console:
    def __init__(self, q):
        self.q = q
        self.cap = None                     # while capturing, the screen comes from there

    def keys(self, s, gap=0.03):
        """Each key (escape sequences whole), `gap` seconds apart."""
        for k in SEQ.findall(s):
            self.q.send(k)
            time.sleep(gap)

    def type(self, text, gap=0.035, chunk=2):
        """Text a few characters at a time (newlines are Enter)."""
        text = text.replace("\n", "\r")
        for i in range(0, len(text), chunk):
            self.q.send(text[i:i + chunk])
            time.sleep(gap)

    def esc(self, wait=0.6):
        self.q.send("\x1b")
        time.sleep(wait)

    def screen(self, cw=8, ch=16):
        if not self.cap:
            return qt.screen_text(self.q.screendump(), cw, ch)
        t = time.time()                     # a frame taken after this call
        while self.cap.latest[0] < t:
            time.sleep(0.02)
        return qt.screen_text(self.cap.latest[1], cw, ch)

    def see(self, words, cw=8, ch=16, timeout=20):
        deadline = time.time() + timeout
        while True:
            text = "\n".join(self.screen(cw, ch))
            if all(w in text for w in words):
                return text
            if time.time() > deadline:
                raise AssertionError(f"{words} not on the screen:\n{text}")
            time.sleep(0.3)


def boot(build, cart, tmp):
    img = os.path.join(tmp, "sd.img")
    cfg = os.path.join(tmp, "config.txt")
    with open(cfg, "w") as f:
        f.write("draw=ram\n")            # whole frames on page 0 (QEMU ignores the page flips)
    cpath = os.path.join(tmp, "myvill.bm")
    with open(cpath, "wb") as f:
        f.write(cart)
    mksd.build(img, [(cpath, "carts/myvill.bm"), (cfg, "bm/config.txt")])
    q = qt.Qemu(os.path.join(build, "kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])
    q.expect(qt.MENU, timeout=40)
    time.sleep(1.0)
    return q


def walk(c, plan):
    """Arrows held: one key every 50 ms for each [key, seconds]."""
    for key, secs in plan:
        keys = key if isinstance(key, tuple) else (key,)
        end = time.time() + secs
        while time.time() < end:
            for k in keys:
                c.q.send(k)
                time.sleep(0.05 / len(keys))


def test_game(build, out):
    """--test: the final cartridge built directly, played, a few pictures."""
    data = start_cart()
    cart = rebuild(data, lua=final_lua(), map_=planned_map())
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "test.lua"), "w") as f:
        f.write(final_lua())
    with tempfile.TemporaryDirectory() as tmp:
        q = boot(build, cart, tmp)
        try:
            c = Console(q)
            c.keys("\r", 0.5)                       # play the only cover
            q.expect("playing myvill.bm", timeout=10)
            time.sleep(3)
            for i, (key, secs) in enumerate([(None, 0), (RIGHT, 1.5), (UP, 1.5), (LEFT, 1.0), (DOWN, 1.0)]):
                if key:
                    walk(c, [(key, secs)])
                qt._save_png(q.screendump(), os.path.join(out, f"test{i}.png"))
            q.send("q")
            out_ = q.expect("update+draw", timeout=20).decode(errors="replace")
            print(out_[-400:])
        finally:
            q.close()


def scenes(build, out):
    """The map in the SDK, the code in bm Code with the assistant, the game."""
    shutil.rmtree(out, ignore_errors=True)
    frames = os.path.join(out, "frames")
    os.makedirs(frames)
    data = start_cart()
    with tempfile.TemporaryDirectory() as tmp:
        q = boot(build, data, tmp)
        c = Console(q)
        cap = Capture(q, frames)
        c.cap = cap
        try:
            cap.start()
            time.sleep(1.0)

            # ---------------------------------------------- the map (SDK)
            c.keys("x", 0.5)                            # the options of "My Village"
            c.keys("s", 0.4)
            c.keys(ENTER, 0.3)                          # Open in the SDK
            c.see(["opened /carts/myvill.bm"])
            cap.mark("map")
            c.keys(F3, 0.8)                             # the sprites, then the map page
            c.keys(F3, 0.8)
            c.see(["layer 1/1 main"])
            c.keys("f", 1.2)                            # grass (tile 1) everywhere
            c.keys(TAB, 0.4)                            # the tile picker: the sand
            c.keys(RIGHT * (SAND - GRASS), 0.04)
            c.keys(TAB, 0.4)
            c.keys(DOWN * PATH_ROWS[0], 0.05)
            for row, step in ((PATH_ROWS[0], RIGHT), (PATH_ROWS[1], LEFT)):
                for i in range(W):
                    c.keys(" " + (step if i < W - 1 else ""), 0.025)
                if row == PATH_ROWS[0]:
                    c.keys(DOWN, 0.05)
            # the pond, then the field: a rectangle each, row by row
            pos = [0, PATH_ROWS[1]]

            def rect(x0, y0, x1, y1, n, cur):
                c.keys(TAB, 0.3)
                c.keys((RIGHT if n > cur else LEFT) * abs(n - cur), 0.04)
                c.keys(TAB, 0.3)
                dx, dy = x0 - pos[0], y0 - pos[1]
                c.keys((RIGHT if dx > 0 else LEFT) * abs(dx) + (DOWN if dy > 0 else UP) * abs(dy), 0.04)
                pos[:] = [x0, y0]
                for y in range(y0, y1 + 1):
                    xs = range(x0, x1 + 1) if (y - y0) % 2 == 0 else range(x1, x0 - 1, -1)
                    step = RIGHT if (y - y0) % 2 == 0 else LEFT
                    for i, x in enumerate(xs):
                        c.keys(" " + (step if i < len(xs) - 1 else ""), 0.03)
                    pos[0] = xs[-1]
                    if y < y1:
                        c.keys(DOWN, 0.05)
                        pos[1] += 1
            rect(*POND, WATER, SAND)
            rect(*FIELD, EARTH, WATER)
            time.sleep(0.6)
            c.keys(CTRL("s"), 0.5)
            c.see(["saved /carts/MYVILL.BM"])
            time.sleep(0.8)
            cap.mark("map-end")
            c.esc()                                     # the menu: Exit editor
            c.keys(UP, 0.4)
            c.keys(ENTER, 0.5)
            c.see(["last: SDK on myvill.bm"])
            time.sleep(1.0)

            # ---------------------------------------------- the code (bm Code)
            c.keys("x", 0.5)
            c.keys("ss", 0.3)
            c.keys(ENTER, 0.3)                          # Open in bm Code
            q.expect("code: ready", timeout=20)
            time.sleep(0.8)
            cap.mark("code")
            c.keys(END, 0.2)
            c.keys(ENTER * 2, 0.2)
            c.keys(F6, 0.8)                             # the assistant
            c.type("come muovo il personaggio con le frecce", 0.05)
            time.sleep(1.6)
            c.keys(ENTER, 1.0)                          # its code goes in
            cap.mark("code-edit")
            c.keys(CTRL("l"), 0.3)
            c.type("3\n", 0.1)                          # the start: on the path
            c.keys(END, 0.2)
            c.keys(BS * len("320, 180, 2"), 0.03)
            c.type(CODE_TOP.split("= ")[1], 0.06)
            c.keys(CTRL("l"), 0.3)
            c.type("12\n", 0.1)                         # its _draw makes way for a 3D one
            c.keys(CTRL("k") * 4, 0.15)
            c.keys(CTRL("l"), 0.3)
            c.type("11\n", 0.1)
            c.type("\n" + CODE_VARS + "\n" + CODE_INIT + "\n" + CODE_DRAW.rstrip("\n"), 0.03, 3)
            time.sleep(0.6)
            cap.mark("code-ai")
            c.keys(CTRL("l"), 0.3)
            c.type("16\n", 0.1)                         # function _init()
            c.keys(END, 0.2)
            c.keys(ENTER, 0.2)
            c.type("#entry: commenta questa funzione #", 0.05)
            c.keys(ENTER, 1.2)
            q.expect("code: #entry", timeout=20)
            time.sleep(1.2)
            c.keys(CTRL("s"), 0.5)
            q.expect("code: saved /carts/myvill.bm", timeout=20)
            time.sleep(0.8)

            # ---------------------------------------------- the game
            cap.mark("run")
            c.keys(F5, 0.1)                             # saves, then plays
            time.sleep(4.0)
            cap.mark("play")
            time.sleep(1.2)
            # about 6 cells a second: it stays on the 24 x 14 cells of the map
            walk(c, [(RIGHT, 0.8), ((RIGHT, UP), 0.6), (UP, 0.5), (LEFT, 1.5), ((LEFT, DOWN), 0.8),
                     (LEFT, 0.5), (DOWN, 0.4), ((RIGHT, DOWN), 0.5)])
            time.sleep(1.2)
            cap.mark("play-end")
            c.keys("q", 0.2)
            q.expect("update+draw", timeout=20)
            time.sleep(0.5)
        finally:
            cap.stop(os.path.join(out, "frames.json"))
            q.close()
        # the cartridge as the console saved it
        part = os.path.join(tmp, "part.img")
        with open(os.path.join(tmp, "sd.img"), "rb") as f, open(part, "wb") as o:
            f.seek(2048 * 512)
            o.write(f.read())
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
        subprocess.run(["mcopy", "-n", "-i", part, "::/CARTS/MYVILL.BM", os.path.join(out, "myvill.bm")],
                       check=True, env=env)
    lua = dict(sections(open(os.path.join(out, "myvill.bm"), "rb").read()))[mkbm.SEC_LUA].decode()
    with open(os.path.join(out, "main.lua"), "w") as f:
        f.write(lua)
    print(f"console: {len(cap.frames)} frames; the code of the game in {out}/main.lua")


def main():
    build, out = sys.argv[1:3]
    if "--test" in sys.argv:
        test_game(build, out)
    else:
        scenes(build, out)


if __name__ == "__main__":
    main()
