#!/usr/bin/env python3
"""
build.py - bm Tape: joins core/src/*.lua into one main.lua, puts src/engine.b at the placeholder line of the engine file,
makes a blank 16 x 16 sprite sheet (the runtime needs one) and an 88 x 88 cover, and packs the project (.bme) and the
cartridge (.bm) with core/scripts/mkbm.py.

  python3 bm/tape/build/build.py [outdir]      (default build/tape)

Files written in outdir: main.lua, sheet.png, cover.png, bank.json, felucca/ (the WAV files), tape.bme, tape.bm.

The sound bank holds 24 samples rendered on the PC from Felucca-Engines (core/scripts/felpack.py, bm/tape/sounds/felucca.json).
The bank has samples only: no sound, no sfx, no song. The sounds of the cartridge are tone() tables in the Lua source.

Test build: with TAPE_TEST=N in the environment (N = 1, 2, 3 ...) the file bm/tape/test/hook.lua goes in at the line --@TEST_HOOK@.
The cartridge then logs every note the sequencer queues with play_at (lines "TAPE_AT time track note velocity tick level pan room echo chorus",
time in seconds from the transport start) and updates the sequencer only every N-th frame (tests/tape/timing_test.py).
The hook also has F9 (build a test song), F10 (log the state of the song) and logs the level, pan and sends of each note
(tests/tape/song_test.py). Without the variable the line is dropped and the cartridge logs nothing.
"""
import glob, os, struct, subprocess, sys, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
SRC = os.path.join(HERE, "..", "src")
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "tape")
os.makedirs(OUT, exist_ok=True)

PLACEHOLDER = "--@ENGINE_B@"
HOOK = "--@TEST_HOOK@"
CLOSE = "]=====]"           # the long string of 10_engine.lua has level 5: engine.b must not hold this


def png(path, w, h, pixel):
    raw = b"".join(b"\x00" + b"".join(pixel(x, y) for x in range(w)) for y in range(h))
    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    open(path, "wb").write(data)


def cover(path):
    """An 88x88 cover: a banded sun over a perspective grid, with a cassette label in three colours. Original artwork."""
    ink, grid, dim = (12, 8, 32), (75, 61, 150), (44, 35, 96)
    hot, amber, cyan, pink = (255, 106, 61), (255, 197, 61), (63, 232, 245), (255, 79, 174)
    hz, W = 58, 88

    def scene(x, y):
        if y < hz:
            if (x - 44) ** 2 + (y - 38) ** 2 < 24 ** 2:
                if y > 38 and (y - 38) % 6 < (y - 38) // 5:         # bands cut into the lower half of the sun
                    return ink
                return amber if y < 38 else hot
            return ink
        v = y - hz
        if v == 0:
            return grid
        if v in (3, 8, 15, 24):                                      # rows of the ground
            return dim
        if any(abs((x - 44) - k * v * 0.7) < 0.7 for k in range(-8, 9)):   # columns meet at (44, hz)
            return dim
        return ink

    def pixel(x, y):
        c = scene(x, y)
        if 10 <= y < 15 and 14 <= x < 74:                            # the label: three bars
            c = (cyan, pink, amber)[(x - 14) // 20]
        return bytes(c) + b"\xff"
    png(path, W, W, pixel)


def main():
    parts = sorted(glob.glob(os.path.join(SRC, "*.lua")))
    lua = "\n".join(open(p).read() for p in parts)
    engine = open(os.path.join(SRC, "engine.b")).read()
    if CLOSE in engine:
        raise SystemExit("engine.b holds the closing bracket of the long string: " + CLOSE)
    if lua.count(PLACEHOLDER) != 1:
        raise SystemExit("main.lua must hold the line %s once" % PLACEHOLDER)
    lua = lua.replace(PLACEHOLDER, engine.rstrip("\n"))
    n = os.environ.get("TAPE_TEST")
    hook = open(os.path.join(HERE, "..", "test", "hook.lua")).read().replace("@N@", str(int(n))) if n else ""
    if lua.count(HOOK) != 1:
        raise SystemExit("main.lua must hold the line %s once" % HOOK)
    lua = lua.replace(HOOK, hook.rstrip("\n"))
    open(os.path.join(OUT, "main.lua"), "w").write(lua)
    sheet = os.path.join(OUT, "sheet.png")
    png(sheet, 16, 16, lambda x, y: b"\x00\x00\x00\x00")
    cv = os.path.join(OUT, "cover.png")
    cover(cv)
    subprocess.check_call([sys.executable, os.path.join(ROOT, "core", "scripts", "felpack.py"), OUT])
    mk = os.path.join(ROOT, "core", "scripts", "mkbm.py")
    for ext in ("bme", "bm"):
        subprocess.check_call([sys.executable, mk, "-o", os.path.join(OUT, "tape." + ext),
                               "--lua", os.path.join(OUT, "main.lua"), "--sheet", sheet, "--sheet8",
                               "--cover", cv, "--audio", os.path.join(OUT, "bank.json"), "--info", os.path.join(HERE, "..", "bm.info")])
    print("built", OUT)


main()
