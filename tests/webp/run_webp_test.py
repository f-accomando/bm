#!/usr/bin/env python3
"""Tests of src/bm/webp.c through build/host/test_webp.

  run_webp_test.py BINARY [--regen]

The fixtures (tests/webp/fixtures/NAME.webp and NAME.ref.z) are small pictures. The .ref.z file is the
picture as decoded by libwebp (through Pillow), raw RGB, 3 bytes a pixel, zlib packed. With Pillow and WebP
support installed, the test also makes new pictures (several sizes, qualities and encoder
options) and compares them. --regen rewrites the fixtures from the generator.

Lossless pictures must match to the bit (after the cut to RGB565). Lossy pictures differ a little
(the colour conversion and the chroma smoothing are not libwebp's code): the mean error and the
share of large errors are bounded.
"""
import os
import subprocess
import zlib
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
FIX = os.path.join(HERE, "fixtures")

try:
    from PIL import Image, ImageDraw, features
    HAVE_PIL = bool(features.check("webp"))
except Exception:  # Pillow missing
    HAVE_PIL = False

fails = 0


def check(cond, msg):
    global fails
    print(("ok   " if cond else "FAIL ") + msg)
    if not cond:
        fails += 1


def run(binary, *args):
    r = subprocess.run([binary, *args], capture_output=True, text=True)
    return r.returncode, (r.stdout + r.stderr).strip()


def expand(v):
    r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
    return (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)


def compare(binary, path, ref_rgb, w, h, lossless, name):
    with tempfile.TemporaryDirectory() as td:
        out = os.path.join(td, "o.raw")
        rc, txt = run(binary, "dec", path, out)
        check(rc == 0 and txt == "ok %d %d" % (w, h), "%s: decodes (%s)" % (name, txt))
        if rc:
            return
        raw = open(out, "rb").read()
        check(len(raw) == w * h * 2, "%s: %d bytes" % (name, len(raw)))
        n = w * h
        tot = big = exact = worst = 0
        for i in range(n):
            v = raw[2 * i] | (raw[2 * i + 1] << 8)
            rr, rg, rb = ref_rgb[3 * i], ref_rgb[3 * i + 1], ref_rgb[3 * i + 2]
            if lossless:
                if v == (((rr >> 3) << 11) | ((rg >> 2) << 5) | (rb >> 3)):
                    exact += 1
            else:
                # against the reference cut to RGB565 and expanded the same way: the cut error is not counted
                e = expand(v)
                f = expand(((rr >> 3) << 11) | ((rg >> 2) << 5) | (rb >> 3))
                d = abs(e[0] - f[0]) + abs(e[1] - f[1]) + abs(e[2] - f[2])
                tot += d
                worst = max(worst, d)
                if d > 24:
                    big += 1
        if lossless:
            check(exact == n, "%s: %d of %d pixels exact" % (name, exact, n))
        else:
            mean = tot / (3.0 * n)
            check(mean <= 1.0 and big <= n * 0.01,
                  "%s: mean error %.2f, worst %d, %d large (%.2f%%)" % (name, mean, worst, big, 100.0 * big / n))


def make_picture(w, h, seed):
    import random
    rnd = random.Random(seed)
    im = Image.new("RGB", (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = ((x * 255) // max(1, w - 1), (y * 255) // max(1, h - 1), ((x + y) * 3) & 255)
    d = ImageDraw.Draw(im)
    for _ in range(6):
        c = (rnd.randrange(256), rnd.randrange(256), rnd.randrange(256))
        x0, y0 = rnd.randrange(w), rnd.randrange(h)
        x1, y1 = x0 + rnd.randrange(1, max(2, w)), y0 + rnd.randrange(1, max(2, h))
        if rnd.random() < 0.5:
            d.rectangle([x0, y0, x1, y1], fill=c)
        else:
            d.ellipse([x0, y0, x1, y1], fill=c)
    d.line([0, 0, w, h], fill=(255, 255, 255), width=2)
    for _ in range(w * h // 30):                  # noise
        px[rnd.randrange(w), rnd.randrange(h)] = (rnd.randrange(256), rnd.randrange(256), rnd.randrange(256))
    return im


# (name, size, save options)
CASES = [
    ("lossy_q75_64x48", (64, 48), dict(quality=75)),
    ("lossy_q20_33x17", (33, 17), dict(quality=20)),
    ("lossy_q95_80x80", (80, 80), dict(quality=95, method=6)),
    ("lossy_q50_1x1", (1, 1), dict(quality=50)),
    ("lossy_q60_16x16", (16, 16), dict(quality=60)),
    ("lossy_q10_97x61_sharp", (97, 61), dict(quality=10, method=6)),
    ("lossy_simple_50x50", (50, 50), dict(quality=70, method=0)),
    ("lossy_q100_40x40", (40, 40), dict(quality=100)),
    ("lossless_64x48", (64, 48), dict(lossless=True)),
    ("lossless_33x17_m6", (33, 17), dict(lossless=True, quality=100, method=6)),
    ("lossless_1x1", (1, 1), dict(lossless=True)),
    ("lossless_pal_40x40", (40, 40), dict(lossless=True, quality=0, method=0)),
    ("lossless_90x70_m3", (90, 70), dict(lossless=True, quality=60, method=3)),
]


# VP8 streams of libvpx (through ffmpeg): (name, size, profile, token partitions, sharpness, crf)
VP8_CASES = [
    ("vpx_p0_s0_64x64", (64, 64), 0, 0, 0, 30),
    ("vpx_p1_simple_72x56", (72, 56), 1, 0, 0, 20),
    ("vpx_p0_parts4_sharp_96x80", (96, 80), 0, 2, 5, 25),
    ("vpx_p0_parts8_q5_48x32", (48, 32), 0, 3, 7, 5),
    ("vpx_p3_crf55_40x40", (40, 40), 3, 1, 2, 55),
]


# pictures of ffmpeg's test sources, saved by Pillow: (name, lavfi source, save options)
LAVFI_CASES = [
    ("lav_mandel_lossless_192x128", "mandelbrot=size=192x128", dict(lossless=True, quality=100, method=6)),
    ("lav_mandel_lossless_fast", "mandelbrot=size=96x64", dict(lossless=True, quality=20, method=1)),
    ("lav_testsrc2_lossless_160x120", "testsrc2=size=160x120", dict(lossless=True, quality=100, method=6)),
    ("lav_testsrc2_lossy_176x144", "testsrc2=size=176x144", dict(quality=80, method=6)),
    ("lav_life_lossless_128x96", "life=size=128x96:ratio=0.2:mold=10", dict(lossless=True, quality=75, method=4)),
    ("lav_gradients_lossy_150x100", "gradients=size=150x100:seed=3:nb_colors=5", dict(quality=60)),
    ("lav_gradients_lossless_150x100", "gradients=size=150x100:seed=3:nb_colors=5", dict(lossless=True, quality=100, method=6)),
]


def regen_lavfi():
    import shutil
    if not shutil.which("ffmpeg"):
        print("no ffmpeg: the ffmpeg source fixtures are kept")
        return
    for name, src, opts in LAVFI_CASES:
        with tempfile.TemporaryDirectory() as td:
            png = os.path.join(td, "in.png")
            subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i", src, "-frames:v", "1", png], check=True)
            im = Image.open(png).convert("RGB")
        p = os.path.join(FIX, name + ".webp")
        im.save(p, "WEBP", **opts)
        save_ref(name, Image.open(p).convert("RGB").tobytes())
        print("wrote", name, os.path.getsize(p), "bytes")


def wrap_vp8(frame):
    pad = len(frame) & 1
    chunk = b"VP8 " + len(frame).to_bytes(4, "little") + frame + bytes(pad)
    return b"RIFF" + (4 + len(chunk)).to_bytes(4, "little") + b"WEBP" + chunk


def regen_vpx():
    import shutil
    if not shutil.which("ffmpeg"):
        print("no ffmpeg: the libvpx fixtures are kept")
        return
    for i, (name, size, profile, parts, sharp, crf) in enumerate(VP8_CASES):
        with tempfile.TemporaryDirectory() as td:
            src = os.path.join(td, "in.png")
            make_picture(size[0], size[1], 900 + i).save(src)
            ivf = os.path.join(td, "o.ivf")
            subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", src, "-c:v", "libvpx", "-profile:v", str(profile),
                            "-slices", str(1 << parts), "-sharpness", str(sharp), "-crf", str(crf), "-b:v", "0",
                            "-g", "1", "-auto-alt-ref", "0", "-frames:v", "1", "-f", "ivf", ivf], check=True)
            data = open(ivf, "rb").read()
            n = int.from_bytes(data[32:36], "little")
            frame = data[44:44 + n]
        p = os.path.join(FIX, name + ".webp")
        open(p, "wb").write(wrap_vp8(frame))
        ref = Image.open(p).convert("RGB")
        save_ref(name, ref.tobytes())
        print("wrote", name, os.path.getsize(p), "bytes, tag", frame[0] & 0xff)


def save_ref(name, raw):
    open(os.path.join(FIX, name + ".ref.z"), "wb").write(zlib.compress(raw, 9))


def regen():
    os.makedirs(FIX, exist_ok=True)
    for i, (name, size, opts) in enumerate(CASES):
        im = make_picture(size[0], size[1], 100 + i)
        if "pal" in name:
            im = im.quantize(16).convert("RGB")
        p = os.path.join(FIX, name + ".webp")
        im.save(p, "WEBP", **opts)
        ref = Image.open(p).convert("RGB")
        save_ref(name, ref.tobytes())
        print("wrote", name, os.path.getsize(p), "bytes")
    regen_vpx()
    regen_lavfi()


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    binary = sys.argv[1]
    if "--regen" in sys.argv:
        if not HAVE_PIL:
            print("Pillow with WebP is needed for --regen")
            return 2
        regen()
        return 0

    names = sorted(f[:-5] for f in os.listdir(FIX) if f.endswith(".webp"))
    check(len(names) > 0, "fixtures found: %d" % len(names))
    for name in names:
        path = os.path.join(FIX, name + ".webp")
        ref = zlib.decompress(open(os.path.join(FIX, name + ".ref.z"), "rb").read())
        rc, txt = run(binary, "info", path)
        w, h = (int(x) for x in txt.split()) if rc == 0 else (0, 0)
        check(rc == 0 and len(ref) == w * h * 3, "%s: info %s" % (name, txt))
        compare(binary, path, ref, w, h, "lossless" in name, name)
        rc, txt = run(binary, "crop", path)
        check(rc == 0 and txt.startswith("crop-ok"), "%s: crop %s" % (name, txt))
        rc, txt = run(binary, "fuzz", path, "150")
        check(rc == 0 and txt.startswith("fuzz-ok"), "%s: fuzz %s" % (name, txt))

    if HAVE_PIL:
        with tempfile.TemporaryDirectory() as td:
            k = 0
            for size in ((8, 8), (17, 31), (130, 70), (255, 20)):
                for opts in (dict(quality=40), dict(quality=90, method=4), dict(lossless=True),
                             dict(lossless=True, quality=100, method=6)):
                    k += 1
                    im = make_picture(size[0], size[1], 5000 + k)
                    p = os.path.join(td, "g%d.webp" % k)
                    im.save(p, "WEBP", **opts)
                    ref = Image.open(p).convert("RGB").tobytes()
                    compare(binary, p, ref, size[0], size[1], bool(opts.get("lossless")),
                            "gen %dx%d %s" % (size[0], size[1], sorted(opts.items())))
            # palettes of every packing width, flat and repeated pictures, a larger picture
            extra = []
            for ncol in (2, 3, 4, 7, 16, 17, 200):
                extra.append(("palette%d" % ncol, make_picture(61, 37, 700 + ncol).quantize(ncol).convert("RGB"),
                              dict(lossless=True, quality=100, method=6)))
            extra.append(("flat", Image.new("RGB", (70, 50), (12, 200, 99)), dict(lossless=True)))
            tile = make_picture(16, 16, 77)
            rep = Image.new("RGB", (160, 96))
            for ty in range(0, 96, 16):
                for tx in range(0, 160, 16):
                    rep.paste(tile, (tx, ty))
            extra.append(("tiles", rep, dict(lossless=True, quality=100, method=6)))
            extra.append(("tiles_fast", rep, dict(lossless=True, quality=10, method=1)))
            extra.append(("big", make_picture(320, 240, 4242), dict(lossless=True, quality=100, method=6)))
            extra.append(("big_lossy", make_picture(320, 240, 4243), dict(quality=80, method=6)))
            # a picture with an alpha channel: the container has VP8X (and ALPH); the colour is decoded, the alpha not
            rgba = make_picture(48, 40, 31).convert("RGBA")
            rgba.putalpha(Image.linear_gradient("L").resize((48, 40)))
            extra.append(("alpha_lossy", rgba, dict(quality=80)))
            extra.append(("alpha_lossless", rgba, dict(lossless=True, exact=True)))
            for name, im, opts in extra:
                p = os.path.join(td, name + ".webp")
                im.save(p, "WEBP", **opts)
                ref = Image.open(p).convert("RGB").tobytes()
                compare(binary, p, ref, im.size[0], im.size[1], bool(opts.get("lossless")), "gen " + name)
    else:
        print("note: Pillow with WebP not found, only the fixtures ran")

    # things that are not pictures
    with tempfile.TemporaryDirectory() as td:
        for name, data in (("empty", b""), ("riff", b"RIFF\x04\x00\x00\x00WEBP"), ("junk", bytes(range(256)) * 4),
                           ("png", b"\x89PNG\r\n\x1a\n" + bytes(64)),
                           ("noimage", b"RIFF\x1a\x00\x00\x00WEBPEXIF\x0a\x00\x00\x00" + bytes(10) + b"\x00" * 4)):
            p = os.path.join(td, name)
            open(p, "wb").write(data)
            rc, txt = run(binary, "info", p)
            check(rc == 1 and txt == "bad", "%s is refused" % name)
        if names:
            # the same picture with the animation flag set in a VP8X chunk is refused
            body = open(os.path.join(FIX, names[0] + ".webp"), "rb").read()[12:]
            vp8x = b"VP8X" + (10).to_bytes(4, "little") + bytes([0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0])
            anim = b"RIFF" + (4 + len(vp8x) + len(body)).to_bytes(4, "little") + b"WEBP" + vp8x + body
            p = os.path.join(td, "anim")
            open(p, "wb").write(anim)
            rc, txt = run(binary, "info", p)
            check(rc == 1, "an animation is refused")
            vp8x = vp8x[:8] + bytes([0x10]) + vp8x[9:]
            p = os.path.join(td, "alpha")
            open(p, "wb").write(b"RIFF" + (4 + len(vp8x) + len(body)).to_bytes(4, "little") + b"WEBP" + vp8x + body)
            rc, txt = run(binary, "info", p)
            check(rc == 0, "a VP8X chunk without animation is read (%s)" % txt)

    print("webp: %s" % ("PASS" if fails == 0 else "%d FAILED" % fails))
    return 1 if fails else 0


sys.exit(main())
