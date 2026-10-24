"""Checks of the simple sections (everything but MESH and ANIM).

Each `check_<name>(body, where)` raises ValidationError or returns a small
parsed view (dict). They are ports of the C checks in src/bm/format.c; the
bodies stay raw bytes in a Cart (a rewrite copies them unchanged).
Not checked deeper: LUA (any bytes, like the C parser, src/bm/format.c:608-611)
and the AUDIO bank (only the "BMAU" magic and size >= 16, src/bm/format.c:668;
the full bank is scripts/bmaudio.py unpack, not ported: NON VERIFICATO beyond
the magic).
"""
import struct

from .binary import read_name
from .errors import ValidationError, need

# section types, src/bm/bm.h:143-156
LUA, SHEET, MAP, COVER, SHEET8, AUDIO, OLD_ANIM, MESH, ANIM = 1, 2, 3, 4, 5, 6, 7, 8, 9
INFO, SPRITES, LAYERS, FLAGS, BOXES = 10, 11, 12, 13, 14
NAMES = {LUA: "LUA", SHEET: "SHEET", MAP: "MAP", COVER: "COVER", SHEET8: "SHEET8", AUDIO: "AUDIO",
         OLD_ANIM: "OLD_ANIM", MESH: "MESH", ANIM: "ANIM", INFO: "INFO", SPRITES: "SPRITES",
         LAYERS: "LAYERS", FLAGS: "FLAGS", BOXES: "BOXES"}
KNOWN = frozenset(NAMES)

INFO_MAX = 16 * 1024        # BM_INFO_MAX, bm.h:159
SPRITES_MAX = 1024          # bm.h:160
SPRITE_SIZE = 28            # bm.h:161
FRAMES_MAX = 16             # bm.h:162
BOXES_MAX = 4096            # bm.h:163
BOX_SIZE = 28               # bm.h:164
LAYERS_MAX = 8              # bm.h:157
LAYER_NAME = 16             # bm.h:158
SHEET_MAX = 4096            # bm.h:169
COVER_MAX = 512             # src/bm/format.c:662


def sec_name(t):
    return NAMES.get(t, f"type {t}")


def _wh(body, where, what):
    need(len(body) >= 4, f"{what} shorter than its 4-byte head", where)
    return struct.unpack_from("<HH", body, 0)


def check_info(body, where="INFO"):
    """INFO (10): UTF-8 text <= 16 KB (src/bm/format.c:577-585, bm.h:80-86).
    C does not check UTF-8; bm-forge refuses invalid UTF-8 (strict choice).
    -> {"text": str}"""
    need(len(body) <= INFO_MAX, f"INFO is {len(body)} bytes, at most {INFO_MAX}", where)
    try:
        return {"text": body.decode("utf-8")}
    except UnicodeDecodeError as e:
        raise ValidationError(f"INFO is not valid UTF-8 ({e.reason} at {e.start})", where)


def check_sprites(body, where="SPRITES"):
    """SPRITES (11): port of sprites_check (src/bm/format.c:469-485), layout
    bm.h:87-91. -> {"zones": [{name, x, y, w, h, frames, fps[, slices]}]}"""
    need(len(body) >= 4, "SPRITES shorter than its head", where)
    n, pad = struct.unpack_from("<HH", body, 0)
    need(1 <= n <= SPRITES_MAX, f"{n} sprite zones, 1 to {SPRITES_MAX}", where)
    need(len(body) == 4 + n * SPRITE_SIZE, f"SPRITES size {len(body)} != 4 + 28 x {n}", where)
    zones, seen = [], set()
    for i in range(n):
        z = body[4 + i * SPRITE_SIZE:4 + (i + 1) * SPRITE_SIZE]
        w_ = f"{where} zone {i}"
        need(z[0] != 0, "empty zone name", w_)
        name = read_name(z[:16], w_, "zone name")
        need(z[:16] not in seen, f"zone name {name!r} twice", w_)
        seen.add(z[:16])
        x, y, w, h, frames, fps, slices = struct.unpack_from("<HHHHBBB", z, 16)
        need(w and h, "zone of size 0", w_)
        need(1 <= frames <= FRAMES_MAX, f"{frames} frames, 1 to {FRAMES_MAX}", w_)
        zone = {"name": name, "x": x, "y": y, "w": w, "h": h, "frames": frames, "fps": fps}
        if slices:                      # byte 26: slices of a stack zone
            zone["slices"] = slices
        zones.append(zone)
    return {"zones": zones}


def check_boxes(body, where="BOXES"):
    """BOXES (14): port of boxes_check (src/bm/format.c:489-502), layout bm.h:103-109.
    -> {"boxes": [{zone, frame, kind, x, y, w, h}]}"""
    need(len(body) >= 4, "BOXES shorter than its head", where)
    n = struct.unpack_from("<H", body, 0)[0]
    need(1 <= n <= BOXES_MAX, f"{n} boxes, 1 to {BOXES_MAX}", where)
    need(len(body) == 4 + n * BOX_SIZE, f"BOXES size {len(body)} != 4 + 28 x {n}", where)
    out = []
    for i in range(n):
        b = body[4 + i * BOX_SIZE:4 + (i + 1) * BOX_SIZE]
        w_ = f"{where} box {i}"
        need(b[0] != 0, "empty zone name", w_)
        zone = read_name(b[:16], w_, "zone name")
        frame, kind, x, y, w, h, _ = struct.unpack_from("<BBhhHHH", b, 16)
        need(frame <= FRAMES_MAX, f"frame {frame}, 0 to {FRAMES_MAX}", w_)
        need(w and h, "box of size 0", w_)
        out.append({"zone": zone, "frame": frame, "kind": kind, "x": x, "y": y, "w": w, "h": h})
    return {"boxes": out}


def check_layers(body, where="LAYERS"):
    """LAYERS (12): port of layers_check (src/bm/format.c:506-523), layout
    bm.h:92-97. The MAP size match is checked by the container.
    -> {"w", "h", "names"}"""
    need(len(body) >= 8, "LAYERS shorter than its head", where)
    w, h, n, _ = struct.unpack_from("<HHHH", body, 0)
    need(w and h, "LAYERS of size 0", where)
    need(2 <= n <= LAYERS_MAX, f"{n} layers, 2 to {LAYERS_MAX}", where)
    need(len(body) == 8 + n * LAYER_NAME + (n - 1) * w * h * 2,
         f"LAYERS size {len(body)} does not fit {n} layers of {w}x{h}", where)
    names, seen = [], set()
    for i in range(n):
        raw = body[8 + i * LAYER_NAME:8 + (i + 1) * LAYER_NAME]
        need(raw[0] != 0, f"layer {i}: empty name", where)
        need(raw not in seen, f"layer {i}: name twice", where)
        seen.add(raw)
        names.append(read_name(raw, where, f"layer {i} name"))
    return {"w": w, "h": h, "names": names}


def check_flags(body, where="FLAGS"):
    """FLAGS (13): src/bm/format.c:651-657, layout bm.h:98-102. -> {"per_row", "rows"}"""
    per_row, rows = _wh(body, where, "FLAGS")
    need(per_row >= 1, "FLAGS with 0 cells per row", where)
    need(len(body) == 4 + per_row * rows, f"FLAGS size {len(body)} != 4 + {per_row} x {rows}", where)
    return {"per_row": per_row, "rows": rows}


def check_sheet(body, where="SHEET"):
    """SHEET (2): src/bm/format.c:612-620. -> {"w", "h"}"""
    w, h = _wh(body, where, "SHEET")
    need(1 <= w <= SHEET_MAX and 1 <= h <= SHEET_MAX, f"sheet {w}x{h}, 1..{SHEET_MAX}", where)
    need(len(body) == 4 + w * h * 4, f"SHEET size {len(body)} != 4 + {w}x{h}x4", where)
    return {"w": w, "h": h}


def check_sheet8(body, where="SHEET8"):
    """SHEET8 (5): port of sheet8_walk without drawing (src/bm/format.c:37-63,
    621-634). -> {"w", "h", "ncol"}"""
    need(len(body) >= 12, "SHEET8 shorter than 12 bytes", where)
    w, h, ncol = struct.unpack_from("<HHH", body, 0)
    need(1 <= w <= SHEET_MAX and 1 <= h <= SHEET_MAX, f"sheet {w}x{h}, 1..{SHEET_MAX}", where)
    need(1 <= ncol <= 256, f"{ncol} colours, 1 to 256", where)
    need(8 + ncol * 4 <= len(body), "SHEET8 palette past the end", where)
    q, end, n, i = 8 + ncol * 4, len(body), w * h, 0
    while i < n:
        need(q < end, "SHEET8 runs end before the last pixel", where)
        t = body[q]
        q += 1
        lit = t < 128
        run = t + 1 if lit else t - 126
        need(i + run <= n and q + (run if lit else 1) <= end, "SHEET8 run past the end", where)
        idx = body[q:q + run] if lit else body[q:q + 1]
        need(max(idx) < ncol, "SHEET8 index past the palette", where)
        q += run if lit else 1
        i += run
    need(q == end, f"SHEET8 has {end - q} bytes after the last pixel", where)
    return {"w": w, "h": h, "ncol": ncol}


def check_map(body, where="MAP"):
    """MAP (3): src/bm/format.c:635-642. -> {"w", "h"}"""
    w, h = _wh(body, where, "MAP")
    need(w and h, "MAP of size 0", where)
    need(len(body) == 4 + w * h * 2, f"MAP size {len(body)} != 4 + {w}x{h}x2", where)
    return {"w": w, "h": h}


def check_cover(body, where="COVER"):
    """COVER (4): src/bm/format.c:658-666. -> {"w", "h"}"""
    w, h = _wh(body, where, "COVER")
    need(1 <= w <= COVER_MAX and 1 <= h <= COVER_MAX, f"cover {w}x{h}, 1..{COVER_MAX}", where)
    need(len(body) == 4 + w * h * 4, f"COVER size {len(body)} != 4 + {w}x{h}x4", where)
    return {"w": w, "h": h}


def is_audio(body):
    """a type-6 body is a sound bank only with "BMAU" and >= 16 bytes
    (src/bm/format.c:668); otherwise it is the MESH of the first bm Studio
    files (src/bm/format.c:673-676, scripts/bmmesh.py:188-191 cart_sections)"""
    return len(body) >= 16 and body[:4] == b"BMAU"


def check_audio(body, where="AUDIO"):
    """AUDIO (6): magic and size only (see the module note)"""
    need(is_audio(body), "AUDIO without the BMAU magic", where)
    return {"version": body[4]}


def check_lua(body, where="LUA"):
    """LUA (1): any bytes (src/bm/format.c:608-611)"""
    return {"size": len(body)}


SIMPLE = {LUA: check_lua, SHEET: check_sheet, MAP: check_map, COVER: check_cover, SHEET8: check_sheet8,
          AUDIO: check_audio, INFO: check_info, SPRITES: check_sprites, LAYERS: check_layers,
          FLAGS: check_flags, BOXES: check_boxes}
