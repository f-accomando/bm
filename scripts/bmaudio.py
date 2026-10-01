#!/usr/bin/env python3
"""
bmaudio.py - sound banks of bm cartridges (the AUDIO section of a .bm, made
with the Sound editor of the Dev tab or written by hand). Standard library only.

  bmaudio.py pack bank.json -o bank.bmau      text -> the binary section
  bmaudio.py unpack game.bm -o bank.json      a cartridge's bank (or a .bmau) -> text
  bmaudio.py show game.bm                     what a bank holds

mkbm.py --audio bank.json (or bank.bmau) puts a bank into a cartridge.

The text form is JSON. Every step is a short string, "NOTE [SOUND [VOL [FX]]]":
  NOTE   "." nothing new (the note goes on), "off" (released), or a name:
         "C4" (60, middle C), "C#4" / "Db4", "A4" (69, 440 Hz), "C-1".."G9"
  SOUND  the sound (instrument) number, default 0
  VOL    0..255 of the sound's volume, default 255
  FX     "-" or an effect and its amount 0..15: glide3, bend+5, bend-5, vib4,
         trem8, chord1, arp2, fadeout3, fadein3, retrig3, delay8, cut4
         (src/audio/player.h); on a "." step it acts on the note playing

  {
    "sounds":   [{"name": "KICK", "wave": "sine", "duty": 128, "vol": 230,
                  "adsr": [0, 25, 0, 10], "pitch": 24, "pitch_time": 6,
                  "vibrato": [0, 0], "detune": 0}],
    "sfx":      [{"name": "JUMP", "ms": 30, "loop": [0, 0],
                  "steps": ["C4 1", "E4 1", "G4 1 200 bend+5"]}],
    "patterns": [{"steps": 16, "tracks": {"0": ["C2 0", ".", ...], "3": [...]}}],
    "songs":    [{"name": "THEME", "bpm": 120, "swing": 0, "loop": 0,
                  "order": [0, 1, 0, 2]}]
  }
"""
import argparse
import json
import struct
import sys

MAGIC, VERSION = b"BMAU", 1
SEC_AUDIO = 6
WAVES = ["square", "triangle", "saw", "noise", "sine", "metal"]
FX = ["-", "glide", "bend+", "bend-", "vib", "trem", "chord", "arp", "fadeout", "fadein",
      "retrig", "delay", "cut"]
NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
PC = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
OFF = 128
LIMITS = {"sounds": 32, "sfx": 64, "patterns": 64, "songs": 8}


def note_name(n):
    return NAMES[n % 12] + str(n // 12 - 1)


def parse_note(t):
    t = t.strip()
    letter = t[0].upper()
    if letter not in PC:
        raise ValueError(f"bad note {t!r}")
    n, i = PC[letter], 1
    if i < len(t) and t[i] in "#s":
        n, i = n + 1, i + 1
    elif i < len(t) and t[i] == "b":
        n, i = n - 1, i + 1
    if t[i:i + 1] == "-" and len(t) > i + 2:     # "C-4": tracker style
        i += 1
    octave = int(t[i:])
    m = 12 * (octave + 1) + n
    if not 1 <= m <= 127:
        raise ValueError(f"note out of range {t!r}")
    return m


def step_text(note, sound, vol, fx):
    if note == 0 and fx == 0:
        return "."
    s = "." if note == 0 else "off" if note >= OFF else note_name(note)
    if note >= OFF:
        return s
    parts = [s, str(sound), str(vol)]
    if fx >> 4:
        parts.append(f"{FX[fx >> 4]}{fx & 15}")
    elif vol == 255 and sound == 0:
        parts = [s]
    elif vol == 255:
        parts = [s, str(sound)]
    return " ".join(parts)


def parse_step(t):
    w = str(t).split()
    if not w or (w[0] == "." and len(w) == 1):
        return (0, 0, 0, 0)
    if w[0] == ".":
        note = 0
    elif w[0].lower() == "off":
        return (OFF, 0, 255, 0)
    else:
        note = parse_note(w[0])
    sound = int(w[1]) if len(w) > 1 else 0
    vol = int(w[2]) if len(w) > 2 else 255
    fx = 0
    if len(w) > 3 and w[3] != "-":
        f = w[3].lower()
        for k in sorted(range(1, len(FX)), key=lambda i: -len(FX[i])):
            if f.startswith(FX[k]):
                amount = int(f[len(FX[k]):] or 0)
                if not 0 <= amount <= 15:
                    raise ValueError(f"effect amount 0..15: {t!r}")
                fx = k << 4 | amount
                break
        else:
            raise ValueError(f"unknown effect {w[3]!r}")
    if not (0 <= sound < 32 and 0 <= vol <= 255):
        raise ValueError(f"bad step {t!r}")
    return (note, sound, vol, fx)


def name8(s):
    b = s.encode("ascii", "replace")[:8]
    return b + b"\0" * (8 - len(b))


def pack(bank):
    """the JSON dict -> the AUDIO section bytes"""
    sounds, sfx = bank.get("sounds", []), bank.get("sfx", [])
    pats, songs = bank.get("patterns", []), bank.get("songs", [])
    for k, v in (("sounds", sounds), ("sfx", sfx), ("patterns", pats), ("songs", songs)):
        if len(v) > LIMITS[k]:
            raise ValueError(f"at most {LIMITS[k]} {k}")
    out = bytearray(MAGIC + bytes([VERSION, len(sounds), len(sfx), len(pats), len(songs)]) + b"\0" * 7)
    for s in sounds:
        wave = s.get("wave", "square")
        wave = WAVES.index(wave) if isinstance(wave, str) else int(wave)
        a, d, su, r = s.get("adsr", [1, 0, 255, 10])
        vd, vr = s.get("vibrato", [0, 0])
        out += name8(s.get("name", ""))
        out += struct.pack("<7BbBBBb4x", wave, s.get("duty", 128), s.get("vol", 200), a, d, su, r,
                           s.get("pitch", 0), s.get("pitch_time", 0), vd, vr, s.get("detune", 0))
    for x in sfx:
        steps = [parse_step(t) for t in x["steps"]]
        if not 1 <= len(steps) <= 32:
            raise ValueError("a sound effect has 1 to 32 steps")
        ls, le = x.get("loop", [0, 0])
        out += name8(x.get("name", "")) + struct.pack("<HBBB3x", x.get("ms", 50), len(steps), ls, le)
        for st in steps:
            out += bytes(st)
    for p in pats:
        n = p.get("steps", 16)
        tracks = {int(k): [parse_step(t) for t in v] for k, v in p.get("tracks", {}).items()}
        mask = 0
        body = bytearray()
        for t in range(8):
            if t in tracks and any(st[0] or st[3] for st in tracks[t]):
                if len(tracks[t]) > n:
                    raise ValueError(f"track {t} has more than {n} steps")
                mask |= 1 << t
                steps = tracks[t] + [(0, 0, 0, 0)] * (n - len(tracks[t]))
                for st in steps:
                    body += bytes(st)
        if not 1 <= n <= 64:
            raise ValueError("a pattern has 1 to 64 steps")
        out += bytes([n, mask, 0, 0]) + body
    for s in songs:
        order = s["order"]
        if not 1 <= len(order) <= 64:
            raise ValueError("a song has 1 to 64 positions")
        loop = s.get("loop", 0)
        out += name8(s.get("name", "")) + bytes([s.get("bpm", 120), s.get("swing", 0), len(order),
                                                 255 if loop is None else loop, 0, 0, 0, 0]) + bytes(order)
    return bytes(out)


def unpack(data):
    """the AUDIO section bytes -> the JSON dict"""
    if data[:4] != MAGIC or data[4] != VERSION:
        raise ValueError("not a version 1 sound bank")
    ns, nx, np_, ng = data[5:9]
    off = 16
    name = lambda b: b.split(b"\0")[0].decode("ascii", "replace")
    bank = {"sounds": [], "sfx": [], "patterns": [], "songs": []}
    for _ in range(ns):
        f = struct.unpack_from("<8s7BbBBBb4x", data, off)
        off += 24
        bank["sounds"].append({"name": name(f[0]), "wave": WAVES[f[1]] if f[1] < len(WAVES) else f[1],
                               "duty": f[2], "vol": f[3], "adsr": list(f[4:8]), "pitch": f[8],
                               "pitch_time": f[9], "vibrato": [f[10], f[11]], "detune": f[12]})
    for _ in range(nx):
        nm, ms, n, ls, le = struct.unpack_from("<8sHBBB3x", data, off)
        off += 16
        steps = [step_text(*data[off + 4 * i:off + 4 * i + 4]) for i in range(n)]
        off += 4 * n
        bank["sfx"].append({"name": name(nm), "ms": ms, "loop": [ls, le], "steps": steps})
    for _ in range(np_):
        n, mask = data[off], data[off + 1]
        off += 4
        tracks = {}
        for t in range(8):
            if mask >> t & 1:
                tracks[str(t)] = [step_text(*data[off + 4 * i:off + 4 * i + 4]) for i in range(n)]
                off += 4 * n
        bank["patterns"].append({"steps": n, "tracks": tracks})
    for _ in range(ng):
        nm, bpm, swing, n, loop = struct.unpack_from("<8s4B", data, off)
        off += 16
        bank["songs"].append({"name": name(nm), "bpm": bpm, "swing": swing,
                              "loop": None if loop == 255 else loop, "order": list(data[off:off + n])})
        off += n
    return bank


def dumps(bank):
    """JSON with every list of steps, numbers or a pattern's track on one line"""
    def enc(v, ind):
        pad = " " * ind
        if isinstance(v, dict):
            flat = json.dumps(v, separators=(", ", ": "))
            if len(flat) < 170 and not any(isinstance(x, (dict, list)) and len(json.dumps(x)) > 40
                                           for x in v.values()):
                return flat
            items = [f'{pad} {json.dumps(k)}: {enc(x, ind + 1)}' for k, x in v.items()]
            return "{\n" + ",\n".join(items) + "\n" + pad + "}" if items else "{}"
        if isinstance(v, list) and v and all(isinstance(x, dict) for x in v):
            return "[\n" + ",\n".join(pad + " " + enc(x, ind + 1) for x in v) + "\n" + pad + "]"
        return json.dumps(v)
    return enc(bank, 0) + "\n"


def extract(cart):
    """the AUDIO section of a .bm file, or None"""
    count = cart[17]
    for i in range(count):
        typ, off, size, _ = struct.unpack_from("<IIII", cart, 128 + 16 * i)
        if typ == SEC_AUDIO:
            return cart[off:off + size]
    return None


def load(path):
    """a bank from .json, .bmau or .bm -> section bytes"""
    data = open(path, "rb").read()
    if path.lower().endswith(".json"):
        return pack(json.loads(data))
    if data[:4] == MAGIC:
        return data
    sec = extract(data)
    if sec is None:
        raise SystemExit(f"{path}: no sound bank")
    return sec


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("pack")
    p.add_argument("src")
    p.add_argument("-o", "--output", required=True)
    u = sub.add_parser("unpack")
    u.add_argument("src")
    u.add_argument("-o", "--output")
    s = sub.add_parser("show")
    s.add_argument("src")
    a = ap.parse_args()
    data = load(a.src)
    if a.cmd == "pack":
        open(a.output, "wb").write(data)
        print(f"{a.output}: {len(data)} bytes")
    elif a.cmd == "unpack":
        text = dumps(unpack(data))
        if a.output:
            open(a.output, "w").write(text)
        else:
            print(text)
    else:
        b = unpack(data)
        print(f"{len(data)} bytes: {len(b['sounds'])} sounds, {len(b['sfx'])} sound effects, "
              f"{len(b['patterns'])} patterns, {len(b['songs'])} songs")
        for i, x in enumerate(b["sounds"]):
            print(f"  sound {i:2} {x['name']:8} {x['wave']}")
        for i, x in enumerate(b["sfx"]):
            print(f"  sfx   {i:2} {x['name']:8} {len(x['steps'])} steps of {x['ms']} ms")
        for i, x in enumerate(b["songs"]):
            print(f"  song  {i:2} {x['name']:8} {x['bpm']} BPM, patterns {x['order']}")


if __name__ == "__main__":
    sys.exit(main())
