"""RGB images and their files: PNG (zlib) and animated GIF (own LZW encoder).

Standard library only. Output is deterministic: the same pixels give the
same bytes (fixed zlib level, palette built in a fixed order).
"""
import struct
import zlib


class Image:
    """w x h RGB, pixels = list of 0xRRGGBB ints, row by row from the top;
    covered = how many pixels a face wrote (set by the renderer)"""

    def __init__(self, w, h, background=0x000000, pixels=None):
        if not (isinstance(w, int) and isinstance(h, int) and 1 <= w <= 4096 and 1 <= h <= 4096):
            raise ValueError(f"image size {w!r}x{h!r}: integers 1..4096")
        self.w, self.h = w, h
        self.pixels = list(pixels) if pixels is not None else [background] * (w * h)
        if len(self.pixels) != w * h:
            raise ValueError("pixel count does not match the size")
        self.covered = 0

    def get(self, x, y):
        c = self.pixels[y * self.w + x]
        return (c >> 16 & 255, c >> 8 & 255, c & 255)

    def rgb_bytes(self):
        out = bytearray(self.w * self.h * 3)
        i = 0
        for c in self.pixels:
            out[i] = c >> 16 & 255
            out[i + 1] = c >> 8 & 255
            out[i + 2] = c & 255
            i += 3
        return bytes(out)

    def to_png(self):
        return png_bytes(self)

    def save_png(self, path):
        with open(path, "wb") as f:
            f.write(png_bytes(self))


def _chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)


def png_bytes(img):
    """8-bit RGB PNG, filter 0 on every row, zlib level 9"""
    raw = img.rgb_bytes()
    stride = img.w * 3
    rows = bytearray()
    for y in range(img.h):
        rows.append(0)
        rows += raw[y * stride:(y + 1) * stride]
    return (b"\x89PNG\r\n\x1a\n" + _chunk(b"IHDR", struct.pack(">IIBBBBB", img.w, img.h, 8, 2, 0, 0, 0)) +
            _chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + _chunk(b"IEND", b""))


def contact_sheet(images, cols=None, gap=2, background=0x202020):
    """the images side by side in a grid (cols defaults to ~sqrt(n)); all the
    same size"""
    if not images:
        raise ValueError("contact_sheet needs at least one image")
    w, h = images[0].w, images[0].h
    if any(i.w != w or i.h != h for i in images):
        raise ValueError("contact_sheet images must have the same size")
    n = len(images)
    if cols is None:
        cols = 1
        while cols * cols < n:
            cols += 1
    if not (isinstance(cols, int) and cols >= 1):
        raise ValueError("cols must be a positive integer")
    rows = (n + cols - 1) // cols
    out = Image(cols * w + (cols + 1) * gap, rows * h + (rows + 1) * gap, background)
    for k, im in enumerate(images):
        ox, oy = gap + (k % cols) * (w + gap), gap + (k // cols) * (h + gap)
        for y in range(h):
            base = (oy + y) * out.w + ox
            out.pixels[base:base + w] = im.pixels[y * w:(y + 1) * w]
    out.covered = sum(i.covered for i in images)
    return out


# ---- GIF


def _palette(frames):
    """<= 256 colours for all frames: every colour if they fit, else median
    cut (split the box with the widest channel at its weighted median, 256
    boxes, each the weighted mean of its colours). Deterministic: colours are
    sorted before every split. -> (palette list, mapping dict colour -> index)"""
    counts = {}
    for f in frames:
        for c in f.pixels:
            counts[c] = counts.get(c, 0) + 1
    if len(counts) <= 256:
        pal = sorted(counts)
        return pal, {c: i for i, c in enumerate(pal)}
    shifts = (16, 8, 0)

    def spread(box):
        best = (-1, 0)
        for ch, sh in enumerate(shifts):
            vals = [c >> sh & 255 for c in box]
            best = max(best, (max(vals) - min(vals), -ch))
        return best

    boxes = [sorted(counts)]
    spreads = [spread(boxes[0])]
    while len(boxes) < 256:
        i = max(range(len(boxes)), key=lambda k: (spreads[k][0] if len(boxes[k]) > 1 else -1, -k))
        box = boxes[i]
        if len(box) < 2:
            break
        sh = shifts[-spreads[i][1]]
        box.sort(key=lambda c: (c >> sh & 255, c))
        total, run, cut = sum(counts[c] for c in box), 0, 1
        for k, c in enumerate(box[:-1]):
            run += counts[c]
            if run * 2 >= total:
                cut = k + 1
                break
        else:
            cut = len(box) - 1
        boxes[i:i + 1] = [box[:cut], box[cut:]]
        spreads[i:i + 1] = [spread(box[:cut]), spread(box[cut:])]
    pal, mapping = [], {}
    for i, box in enumerate(boxes):
        n = sum(counts[c] for c in box)
        mean = [sum((c >> sh & 255) * counts[c] for c in box) // n for sh in shifts]
        pal.append(mean[0] << 16 | mean[1] << 8 | mean[2])
        for c in box:
            mapping[c] = i
    return pal, mapping


def lzw_encode(indices, min_code=8):
    """GIF LZW (variable code width up to 12 bits, clear at 4096 codes), the
    bits packed LSB first. -> bytes (not yet in sub-blocks)"""
    clear, eoi = 1 << min_code, (1 << min_code) + 1
    out = bytearray()
    acc = nbits = 0
    size = min_code + 1

    def emit(code):
        nonlocal acc, nbits
        acc |= code << nbits
        nbits += size
        while nbits >= 8:
            out.append(acc & 255)
            acc >>= 8
            nbits -= 8

    table = {}
    nxt = eoi + 1
    emit(clear)
    if not indices:
        emit(eoi)
        if nbits:
            out.append(acc & 255)
        return bytes(out)
    prefix = indices[0]
    for k in indices[1:]:
        key = prefix << 8 | k
        c = table.get(key)
        if c is not None:
            prefix = c
            continue
        emit(prefix)
        if nxt < 4096:
            table[key] = nxt
            nxt += 1
            if nxt > (1 << size) and size < 12:
                size += 1
        else:
            emit(clear)
            table.clear()
            nxt = eoi + 1
            size = min_code + 1
        prefix = k
    emit(prefix)
    emit(eoi)
    if nbits:
        out.append(acc & 255)
    return bytes(out)


def gif_bytes(frames, delay_cs=8, loop=0):
    """animated GIF89a: one global palette, NETSCAPE loop (0 = forever),
    each frame full size, delay in 1/100 s"""
    if not frames:
        raise ValueError("a GIF needs at least one frame")
    w, h = frames[0].w, frames[0].h
    if any(f.w != w or f.h != h for f in frames):
        raise ValueError("GIF frames must have the same size")
    if not (isinstance(delay_cs, int) and 0 <= delay_cs <= 65535):
        raise ValueError("delay_cs must be an integer 0..65535")
    pal, mapping = _palette(frames)
    pal_bytes = bytearray()
    for c in pal + [0] * (256 - len(pal)):
        pal_bytes += bytes((c >> 16 & 255, c >> 8 & 255, c & 255))
    out = bytearray(b"GIF89a" + struct.pack("<HHBBB", w, h, 0xF7, 0, 0) + pal_bytes)
    out += b"\x21\xFF\x0BNETSCAPE2.0\x03\x01" + struct.pack("<H", loop) + b"\x00"
    for f in frames:
        out += b"\x21\xF9\x04\x04" + struct.pack("<H", delay_cs) + b"\x00\x00"
        out += b"\x2C" + struct.pack("<HHHHB", 0, 0, w, h, 0)
        data = lzw_encode([mapping[c] for c in f.pixels], 8)
        out.append(8)
        for i in range(0, len(data), 255):
            blk = data[i:i + 255]
            out.append(len(blk))
            out += blk
        out.append(0)
    out.append(0x3B)
    return bytes(out)


def save_gif(path, frames, delay_cs=8, loop=0):
    with open(path, "wb") as f:
        f.write(gif_bytes(frames, delay_cs, loop))
