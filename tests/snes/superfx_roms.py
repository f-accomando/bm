#!/usr/bin/env python3
"""
superfx_roms.py - local smoke test of the Super FX games in bmhost. It needs ROMs that are NOT in the repository.

  BM_SNES_ROMS=/path/to/folder python3 tests/snes/superfx_roms.py build/host/bmhost-bin build/bm/snes.bm [--seconds 30]

The folder holds .sfc or .smc files (a copier header is fine). The script reads the chipset byte of each file ($FFD6 of the LoROM
header, $13 to $1A for a Super FX cartridge) and plays each Super FX ROM for some hundreds of frames with scripted pad input:
it presses START and A now and then. It takes pictures at fixed frames and checks that each picture is not blank (more than
a few colours) and that the pictures change. Nothing of a ROM is kept: the pictures stay in a temporary folder that the script
deletes, and the output has only file names, counts and OK or FAIL. Without the folder or without a Super FX ROM it prints a
message and exits with 0 (skipped). The exit code is 1 only for a FAIL.
"""
import os, shutil, struct, subprocess, sys, tempfile, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
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
    stride, rows, prev = w * bpp, [], bytearray(w * bpp)
    p = 0
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


def chip_of(path):
    d = open(path, "rb").read(0x10000 + 0x200)
    if os.path.getsize(path) % 1024 == 512: d = d[512:]
    if len(d) >= 0x7FC0 + 0x40:
        c = d[0x7FC0 + 0x16]
        t = d[0x7FC0 + 0x15]
        if 0x13 <= c <= 0x1A and (t & 0x0F) in (0, 1): return c
    return None


def colours(png):
    w, h, bpp, rows = png
    seen = set()
    for r in rows:
        for x in range(0, w * bpp, bpp):
            seen.add(r[x:x + 3])
            if len(seen) > 64: return len(seen)
    return len(seen)


def pixel_set(png):
    w, h, bpp, rows = png
    return b"".join(rows)


def diff(a, b):
    n = min(len(a), len(b))
    return sum(1 for i in range(0, n, 3) if a[i:i + 3] != b[i:i + 3])


def play(host, cart, rom, seconds):
    tmp = tempfile.mkdtemp(prefix="superfx-")
    try:
        os.makedirs(tmp + "/sd/docs"); os.makedirs(tmp + "/sd/sfc"); os.makedirs(tmp + "/shots")
        shutil.copy(rom, tmp + "/sd/sfc/game.sfc")
        lines = ["5 shot browse", "10 pad 1 a", "12 pad 1 none"]
        f = 300
        while f < seconds * 60 - 60:                       # START, then A, now and then
            lines += ["%d pad 1 start" % f, "%d pad 1 none" % (f + 3), "%d pad 1 a" % (f + 40), "%d pad 1 none" % (f + 43)]
            f += 90
        shots = [120, 240, 400, 700, 1000, 1400]
        shots = [s for s in shots if s < seconds * 60 - 10]
        lines += ["%d shot s%d" % (s, s) for s in shots]
        lines.sort(key=lambda l: int(l.split()[0]))          # the script wants the frames in order
        open(tmp + "/in.txt", "w").write("\n".join(lines) + "\n")
        subprocess.check_call([host, cart, "--sd", tmp + "/sd", "--seconds", str(seconds), "--shots", tmp + "/shots",
                               "--input", tmp + "/in.txt", "--quiet"], stdout=subprocess.DEVNULL)
        pics = []
        for s in shots:
            p = tmp + "/shots/s%d.png" % s
            if os.path.exists(p): pics.append((s, read_png(p)))
        return pics
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    seconds = 30
    if "--seconds" in sys.argv: seconds = int(sys.argv[sys.argv.index("--seconds") + 1]); args = [a for a in args if a != str(seconds)]
    if len(args) < 2:
        print(__doc__); return 2
    host, cart = args[:2]
    folder = os.environ.get("BM_SNES_ROMS", "")
    if not folder or not os.path.isdir(folder):
        print("skipped: set BM_SNES_ROMS to a folder with the Super FX ROMs (.sfc or .smc); none is in the repository")
        return 0
    roms = [os.path.join(folder, n) for n in sorted(os.listdir(folder)) if n.lower().endswith((".sfc", ".smc"))]
    fx = [(r, chip_of(r)) for r in roms]
    fx = [(r, c) for r, c in fx if c]
    if not fx:
        print("skipped: no Super FX ROM in the folder (chipset byte $13 to $1A at $FFD6): %d file(s) looked at" % len(roms))
        return 0
    fails = 0
    for rom, chip in fx:
        name = os.path.basename(rom)
        pics = play(host, cart, rom, seconds)
        if len(pics) < 3:
            print("FAIL %s: %d pictures" % (name, len(pics))); fails += 1; continue
        cols = [colours(p) for _, p in pics]
        sets = [pixel_set(p) for _, p in pics]
        changes = [diff(sets[i], sets[i + 1]) for i in range(len(sets) - 1)]
        not_blank = max(cols) > 8
        changes_ok = max(changes) > 200
        print("%s %s chipset $%02X: frames %s, colours %s, pixels changed between pictures %s" %
              ("ok  " if not_blank and changes_ok else "FAIL", name, chip, [f for f, _ in pics], cols, changes))
        if not (not_blank and changes_ok): fails += 1
    print("superfx smoke: %d ROM(s), %d failed" % (len(fx), fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
