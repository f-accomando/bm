#!/usr/bin/env python3
"""
render_test.py - the bm Tape cartridge in bmhost: one picture per scene, plus keys and the error path.

  python3 tests/tape/render_test.py build/host/bmhost-bin build/carts/tape.bm

bmhost runs the engine (src/bm/bmnative.c emulates ARMv6 on the PC), so native_run executes here.
Checks: the run ends without a Lua error; each of the nine pictures is not blank, holds the four encoder colours in
the value strip and shows no engine error; the nine pictures differ; an encoder key and the transport (Space) change
the picture; a cartridge with a broken engine.b still draws and shows the error text on the status strip.
"""
import hashlib, os, struct, subprocess, sys, tempfile, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))

INK, HOT = (0x0c, 0x08, 0x20), (0xff, 0x6a, 0x3d)
ENC = [(0x3f, 0xe8, 0xf5), (0xff, 0x4f, 0xae), (0xff, 0xc5, 0x3d), (0xf4, 0xf0, 0xff)]
F1, RIGHT, SPACE, KEY_E = 0x3A, 0x4F, 0x2C, 0x08


def read_png(path):
    d = open(path, "rb").read()
    pos, idat, w = 8, b"", 0
    while pos < len(d):
        n, t = struct.unpack(">I4s", d[pos:pos + 8])
        body = d[pos + 8:pos + 8 + n]
        if t == b"IHDR":
            w, h, bd, ct = struct.unpack(">IIBB", body[:10])
            bpp = {2: 3, 6: 4}[ct]
        elif t == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride, rows, prev, p = w * bpp, [], bytearray(w * bpp), 0
    for _ in range(h):
        f = raw[p]; line = bytearray(raw[p + 1:p + 1 + stride]); p += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]; c = prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line)); prev = line
    return w, h, bpp, rows


def near(c, want, tol=8):
    """The screen keeps 5 or 6 bits per channel: a colour comes back within a few units."""
    return all(abs(a - b) <= tol for a, b in zip(c, want))


def has(seen, want):
    return any(near(c, want) for c in seen)


def colours(img, x0, y0, x1, y1):
    """The set of colours in a rectangle, and how many pixels are not ink."""
    w, h, bpp, rows = img
    seen, lit = set(), 0
    for y in range(y0, min(y1, h)):
        r = rows[y]
        for x in range(x0, min(x1, w)):
            c = tuple(r[x * bpp:x * bpp + 3])
            seen.add(c)
            lit += not near(c, INK)
    return seen, lit


def crop_hash(img, x0, y0, x1, y1):
    w, h, bpp, rows = img
    return hashlib.md5(b"".join(rows[y][x0 * bpp:x1 * bpp] for y in range(y0, y1))).hexdigest()


def run(host, cart, script, shots, seconds):
    os.makedirs(shots, exist_ok=True)
    inp = os.path.join(os.path.dirname(shots), os.path.basename(shots) + ".txt")
    open(inp, "w").write(script)
    p = subprocess.run([host, cart, "--shots", shots, "--input", inp, "--seconds", str(seconds), "--quiet"],
                       capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def main():
    host, cart = sys.argv[1:3]
    tmp = tempfile.mkdtemp(prefix="tape-test-")
    fails = []

    def check(name, ok):
        if not ok: fails.append(name)
        print(("ok   " if ok else "FAIL ") + name)

    # scenes 1-8 by F1-F8, scene 9 by the right arrow after F8; then an encoder, then the transport on scene 5
    lines = []
    for i in range(8):
        at = 30 * (i + 1)
        lines += ["%d keys %d" % (at, F1 + i), "%d keys none" % (at + 2), "%d shot sc%d" % (at + 20, i + 1)]
    lines += ["270 keys %d" % RIGHT, "272 keys none", "290 shot sc9",
              "300 keys %d" % (F1 + 2), "302 keys none", "310 shot env0",
              "312 keys %d" % KEY_E, "400 keys none", "410 shot env1",
              "420 keys %d" % (F1 + 4), "422 keys none", "430 shot seq0",
              "432 keys %d" % SPACE, "434 keys none", "520 shot seq1"]
    code, out = run(host, cart, "\n".join(lines) + "\n", tmp + "/shots", 9)
    print(out.strip().splitlines()[-1] if out.strip() else "(no output)")
    check("bmhost run ends well (no Lua error)", code == 0 and " ok" in out)
    img = {n: read_png("%s/shots/%s.png" % (tmp, n)) for n in
           ["sc%d" % i for i in range(1, 10)] + ["env0", "env1", "seq0", "seq1"]}
    hashes = {}
    for i in range(1, 10):
        im = img["sc%d" % i]
        _, lit = colours(im, 0, 28, 640, 270)
        check("scene %d: the picture is not blank" % i, lit > 1500)
        strip, _ = colours(im, 0, 276, 640, 316)
        for k, c in enumerate(ENC):
            check("scene %d: encoder colour %d in the value strip" % (i, k + 1), has(strip, c))
        err, _ = colours(im, 236, 0, 520, 26)
        check("scene %d: no engine error on the status strip" % i, not has(err, HOT))
        hashes[i] = crop_hash(im, 0, 0, 640, 320)
    check("the nine pictures differ", len(set(hashes.values())) == 9)
    check("an encoder key changes the value strip", crop_hash(img["env0"], 0, 276, 640, 316) != crop_hash(img["env1"], 0, 276, 640, 316))
    check("an encoder key changes the scene", crop_hash(img["env0"], 0, 28, 640, 270) != crop_hash(img["env1"], 0, 28, 640, 270))
    check("Space runs the transport (bar.beat.step changes)", crop_hash(img["seq0"], 420, 0, 520, 26) != crop_hash(img["seq1"], 420, 0, 520, 26))
    # E(6, 16) from the native engine: pulses at steps 0 3 6 8 11 14 (step 0 is the play head, in hot colour)
    w, h, bpp, rows = img["seq0"]
    cell = [tuple(rows[41][(192 + 16 * i + 6) * bpp:(192 + 16 * i + 6) * bpp + 3]) for i in range(16)]
    pink = [i for i in range(16) if near(cell[i], ENC[1])]
    check("SEQ GRID shows the euclidean pattern of the native engine (pink cells %s)" % pink, pink == [3, 6, 8, 11, 14])

    # the error path: a broken engine.b must leave the cartridge drawing, with the error text on the status strip
    bad = open(ROOT + "/build/tape/main.lua").read()
    check("error path: the test finds engine.b in main.lua", "function euclid(k: i32" in bad)
    bad = bad.replace("function euclid(k: i32", "function euclid(k: str")
    open(tmp + "/main.lua", "w").write(bad)
    subprocess.check_call([sys.executable, ROOT + "/scripts/mkbm.py", "-o", tmp + "/bad.bm", "--lua", tmp + "/main.lua",
                           "--sheet", ROOT + "/build/tape/sheet.png", "--sheet8", "--info", ROOT + "/carts/tape/bm.info"],
                          stdout=subprocess.DEVNULL)
    code, out = run(host, tmp + "/bad.bm", "30 keys %d\n32 keys none\n60 shot bad\n" % (F1 + 4), tmp + "/badshots", 2)
    check("error path: the run ends without a Lua error", code == 0 and " ok" in out)
    im = read_png(tmp + "/badshots/bad.png")
    check("error path: the error text is on the status strip", has(colours(im, 236, 0, 520, 26)[0], HOT))
    check("error path: the scene still draws", colours(im, 0, 28, 640, 270)[1] > 1500)

    print("tape render: %s (pictures in %s/shots)" % ("FAIL" if fails else "ok", tmp))
    sys.exit(1 if fails else 0)


main()
