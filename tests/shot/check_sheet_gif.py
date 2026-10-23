#!/usr/bin/env python3
"""tests/shot/check_sheet_gif.py LOG: the "gif:" lines tests/shot/sheet_gif.lua logs. Decodes the GIF bytes
with its own LZW decoder (no Pillow needed; with Pillow a second decoding is compared) and checks the
header, the loop extension, the frame count, the delays, the transparency, the exact pixels of three
frames, the colour reduction above 255 colours and the refusals."""
import io
import struct
import sys


def lzw_decode(mcs, data, npix):
    clear, eoi = 1 << mcs, (1 << mcs) + 1
    table = None
    out = bytearray()
    nbits = mcs + 1
    acc = nacc = pos = 0
    prev = None
    code = -1
    while len(out) <= npix:
        while nacc < nbits:
            if pos >= len(data):
                raise AssertionError("LZW data ends before the end code")
            acc |= data[pos] << nacc
            nacc += 8
            pos += 1
        code = acc & ((1 << nbits) - 1)
        acc >>= nbits
        nacc -= nbits
        if code == clear:
            table = [bytes([i]) for i in range(clear)] + [b"", b""]
            nbits = mcs + 1
            prev = None
            continue
        if code == eoi:
            break
        assert table is not None, "no clear code first"
        if prev is None:
            assert code < clear, "first code after clear is a literal"
            entry = table[code]
        else:
            if code < len(table):
                entry = table[code]
            else:
                assert code == len(table), f"code {code} beyond the table ({len(table)})"
                entry = prev + prev[:1]
            if len(table) < 4096:
                table.append(prev + entry[:1])
        out += entry
        prev = entry
        if len(table) >= (1 << nbits) and nbits < 12:
            nbits += 1
    assert code == eoi, "the end code is missing"
    assert len(out) == npix, f"{len(out)} pixels, expected {npix}"
    return bytes(out)


def decode(hexs):
    d = bytes.fromhex(hexs)
    assert d[:6] == b"GIF89a", "header"
    w, h, flags, bg, _ = struct.unpack("<HHBBB", d[6:13])
    assert flags & 0x80, "a global colour table"
    size = 2 << (flags & 7)
    pal = [tuple(d[13 + 3 * i:16 + 3 * i]) for i in range(size)]
    pos = 13 + 3 * size
    loop = None
    frames = []
    gce = None
    while True:
        b = d[pos]
        if b == 0x3B:
            assert pos == len(d) - 1, "bytes after the trailer"
            break
        if b == 0x21:
            label = d[pos + 1]
            pos += 2
            blocks = []
            while d[pos]:
                blocks.append(d[pos + 1:pos + 1 + d[pos]])
                pos += 1 + d[pos]
            pos += 1
            if label == 0xFF:
                assert blocks[0] == b"NETSCAPE2.0" and blocks[1][0] == 1, "NETSCAPE2.0 loop block"
                loop = struct.unpack("<H", blocks[1][1:3])[0]
            elif label == 0xF9:
                pk, delay, tri = struct.unpack("<BHB", blocks[0])
                gce = {"disposal": pk >> 2 & 7, "tflag": pk & 1, "delay": delay, "tindex": tri}
        elif b == 0x2C:
            ix, iy, iw, ih, ifl = struct.unpack("<HHHHB", d[pos + 1:pos + 10])
            assert (ix, iy, iw, ih) == (0, 0, w, h) and ifl == 0, "image descriptor"
            mcs = d[pos + 10]
            pos += 11
            data = bytearray()
            while d[pos]:
                n = d[pos]
                data += d[pos + 1:pos + 1 + n]
                pos += 1 + n
            pos += 1
            idx = lzw_decode(mcs, bytes(data), w * h)
            assert gce is not None, "a graphic control before each frame"
            frames.append((gce, idx))
            gce = None
        else:
            raise AssertionError(f"unknown block {b:#x} at {pos}")
    return {"w": w, "h": h, "pal": pal, "loop": loop, "frames": frames}


def rgb(c):
    """The RGB the console keeps of 0xRRGGBB: RGB565, widened as shot_png does."""
    r, g, b = c >> 16 & 255, c >> 8 & 255, c & 255
    r, g, b = r >> 3 << 3, g >> 2 << 2, b >> 3 << 3
    return (r | r >> 5, g | g >> 6, b | b >> 5)


def pixels(gif, k):
    """The frame k as rows of an RGB tuple, or None for a transparent pixel."""
    g, idx = gif["frames"][k]
    out = []
    for y in range(gif["h"]):
        row = []
        for x in range(gif["w"]):
            i = idx[y * gif["w"] + x]
            row.append(None if g["tflag"] and i == g["tindex"] else gif["pal"][i])
        out.append(row)
    return out


def main():
    res = {}
    seen_end = False
    for line in open(sys.argv[1], errors="replace"):
        if not line.startswith("gif: "):
            continue
        parts = line.split()
        if parts[1] == "end":
            seen_end = True
        elif parts[2] == "nil":
            res[parts[1]] = ("nil", " ".join(parts[3:]))
        else:
            res[parts[1]] = ("gif", res.get(parts[1], ("gif", ""))[1] + parts[2])
    assert seen_end, "the cart did not finish"

    # three frames, 4 x 3, delay 7: the exact pixels
    cols = [0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF, 0x000000, 0xFFFF00, None]
    gif = decode(res["three"][1])
    assert (gif["w"], gif["h"], len(gif["frames"])) == (4, 3, 3), (gif["w"], gif["h"], len(gif["frames"]))
    assert gif["loop"] == 0, "loops for ever"
    for k, (g, _) in enumerate(gif["frames"]):
        assert g["delay"] == 7 and g["disposal"] == 2 and g["tflag"] == 1, (k, g)
    for f in range(3):
        got = pixels(gif, f)
        for y in range(3):
            for x in range(4):
                c = cols[(x + y * 4 + f * 5) % 7]
                want = None if c is None else rgb(c)
                assert got[y][x] == want, f"three: frame {f} pixel {x},{y} is {got[y][x]}, expected {want}"
    assert len(gif["pal"]) == 8, "6 colours and the transparent one: a table of 8"

    gif = decode(res["one"][1])
    assert len(gif["frames"]) == 1 and gif["frames"][0][0]["delay"] == 10, "one frame, delay 10 by default"

    # noise: 200 colours, 4 x 64 x 64 pixels: the LZW table fills and resets; exact
    gif = decode(res["noise"][1])
    assert (gif["w"], gif["h"], len(gif["frames"])) == (64, 64, 4)
    assert all(g["delay"] == 3 and g["tflag"] == 0 for g, _ in gif["frames"]), "no transparency in an opaque frame"
    s = 12345
    for f in range(4):
        got = pixels(gif, f)
        for y in range(64):
            for x in range(64):
                s = (s * 1103515245 + 12345) % 2147483648
                i = (s >> 8) % 200
                want = rgb(((i * 5) % 256) << 16 | ((i * 37) % 256) << 8 | ((i * 91) % 256))
                assert got[y][x] == want, f"noise: frame {f} pixel {x},{y} is {got[y][x]}, expected {want}"

    # many: 400 colours reduced to 255, and a transparent corner
    gif = decode(res["many"][1])
    assert (gif["w"], gif["h"], len(gif["frames"])) == (20, 20, 1)
    assert len(gif["pal"]) == 256, len(gif["pal"])
    got = pixels(gif, 0)
    assert got[0][0] is None, "the transparent corner stays"
    worst = 0
    for i in range(1, 400):
        want = rgb(((i % 20) * 12) << 16 | ((i // 20) * 12) << 8 | ((i * 7) % 256))
        g = got[i // 20][i % 20]
        assert g is not None
        worst = max(worst, max(abs(a - b) for a, b in zip(g, want)))
    assert worst < 70, f"the reduction is too coarse: {worst}"
    assert len({p for r in got for p in r if p}) > 200, "the 255 colours are used"

    for name, word in (("outside", "outside"), ("size", "same size"), ("empty", "1 to 64"),
                       ("frames", "1 to 64"), ("delay", "delay"), ("toobig", "262144")):
        kind, why = res[name]
        assert kind == "nil" and word in why, (name, res[name])
    gif = decode(res["widest"][1])
    assert (gif["w"], gif["h"]) == (4096, 64), "widest: the size limit is allowed"

    try:
        from PIL import Image
    except ImportError:
        Image = None
    if Image:
        im = Image.open(io.BytesIO(bytes.fromhex(res["three"][1])))
        assert im.n_frames == 3 and im.info.get("loop") == 0, (im.n_frames, im.info)
        im.seek(1)
        assert im.info.get("duration") == 70, im.info
        print("sheet_gif: Pillow agrees")
    print("sheet_gif: ok")


main()
