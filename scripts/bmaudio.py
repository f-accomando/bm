#!/usr/bin/env python3
"""
bmaudio.py - sound banks of bm cartridges (the AUDIO section of a .bm, made
with the Sound editor of the Dev tab or written by hand). Standard library only.

  bmaudio.py pack bank.json -o bank.bmau      text -> the binary section
  bmaudio.py unpack game.bm -o bank.json      a cartridge's bank (or a .bmau) -> text
                                              (its samples as WAV files beside it)
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
                  "vibrato": [0, 0], "detune": 0},
                 {"name": "BASS", "wave": "saw", "adsr": [0, 30, 120, 10],
                  "cutoff": 90, "resonance": 140, "fenv": 40, "fdecay": 20},
                 {"name": "BREAK", "wave": "sample", "sample": "LOOP", "adsr": [0, 0, 255, 5]}],
    "sfx":      [{"name": "JUMP", "ms": 30, "loop": [0, 0],
                  "steps": ["C4 1", "E4 1", "G4 1 200 bend+5"]}],
    "patterns": [{"steps": 16, "tracks": {"0": ["C2 0", ".", ...], "3": [...]}}],
    "songs":    [{"name": "THEME", "bpm": 120, "swing": 0, "loop": 0,
                  "echo": 3, "room": 140, "order": [0, 1, 0, 2]}],
    "samples":  [{"name": "LOOP", "wav": "loop.wav", "root": "C4", "loop": "fwd"}]
  }

The tone of a sound (version 2, registers 11..31 of the voice, synth.h;
every key optional, 0 if missing, the room 40):
  "cutoff" 0 none, 1..255 = 20 Hz..20 kHz   "resonance" 0..255
  "filter" "lp" "bp" "hp" "notch"           "keytrack" true: follows the note
  "fenv" -128..127 (1/16 octave)            "fdecay" 0..255 (as the ADSR)
  "pan" -127..127                           "noise", "drive" 0..255
  "reverb", "echo" 0..255 (the sends)       "lfo" [rate, cutoff, duty] 0..255
  "mod1", "mod2", "moddecay", "feedback" 0..255: the wave's own settings
  (fm: ratio x/16, depth x/32, its fade, feedback; pluck: brightness,
  sustain; supersaw: spread; organ: four drawbars, a nibble each)
  "raw" true: the 8-bit chip voice          "vowel" "a" "e" "i" "o" "u": formants
  "curve" the drive's: "soft" "hard" "fold" "sine" "asym" "cubic"
  "color" the noise mix's: "white" "pink" "brown" "crackle"
  "crush" 1..15 bits (0 none)                "coarse" 1..16: each value held that long
  "trem" 0..15 the LFO on the volume         "duck" 0..15: this sound ducks the others
  "chorus" 0..255 the send to the chorus     "density" 0..255: crackle's clicks (x 8 a second)
A sound with "wave": "sample" plays "sample": a sample of the bank (its
name or number), or of the console's kit ("bd" "sd" "hh" "oh" "cp" "rim"
"tom" "cb", or "kit:N"); "begin" 0..255 where it starts (x/256 of it),
"reverse" true: backwards. The note against the sample's root sets its speed.
A song's "echo" is the echo's time in steps (0: as it is), "room" the
room's size 1..255 (0: as it is).

Samples (version 3 of the bank; a bank with none is written as version 2):
  "wav"        a WAV file (path from the JSON's folder): 8-bit, 16, 24, 32-bit
               integers or 32/64-bit floats, mono or stereo, any rate; its
               "smpl" chunk (root note, first loop) is read too
  "pcm"        or the frames themselves, base64, with "format" ("u8" "s16"
               "s24" "s32" "f32"), "channels" (1, 2) and "rate" (Hz)
  "root"       the note at which it plays at its own speed ("C4" = 60, default)
  "fine"       cents (-100..100)
  "loop"       "off" (it plays once), "fwd", "pingpong"; "loop_start",
               "loop_end" in frames (end 0: the last frame)
  "store"      the format it is stored in ("u8" .. "f32"; default: the WAV's)
  "mono"       true: the two channels of a stereo file mixed into one
The console reads them as 16-bit frames: at most PCM_MAX values (frames x
channels, 4 frames more each) in a bank, 2 MiB of memory: 22 s of mono at
48 kHz, 47 s at 22 kHz (the cartridge itself is on the SD card; the
console's memory is not).
"""
import argparse
import base64
import json
import math
import os
import struct
import sys

MAGIC, VERSION = b"BMAU", 3
VERSION_PLAIN = 2           # a bank without samples: what every console reads
SEC_AUDIO = 6
WAVES = ["square", "triangle", "saw", "noise", "sine", "metal", "fm", "pluck", "supersaw", "organ", "sample",
         "pink", "brown", "crackle"]
FILTERS = ["lp", "bp", "hp", "notch"]
TONE = 21                   # registers 11..31
ROOM_SEND = 40              # the room of a sound with no tone of its own (AU_ROOM_SEND)
# the tone's keys: name, register - 11, signed
TONE_KEYS = [("cutoff", 0, False), ("resonance", 1, False), ("fenv", 3, True), ("fdecay", 4, False),
             ("pan", 5, True), ("mod1", 6, False), ("mod2", 7, False), ("moddecay", 8, False),
             ("noise", 9, False), ("drive", 10, False), ("reverb", 11, False), ("echo", 12, False),
             ("feedback", 16, False)]
FLAG_RAW, FLAG_REVERSE = 0x01, 0x40
VOWELS = ["", "a", "e", "i", "o", "u"]                          # FILTER bits 3-5
CURVES = ["soft", "hard", "fold", "sine", "asym", "cubic"]      # FLAGS bits 3-5: the drive's
COLORS = ["white", "pink", "brown", "crackle"]                  # FLAGS bits 1-2: the noise mix's
FX = ["-", "glide", "bend+", "bend-", "vib", "trem", "chord", "arp", "fadeout", "fadein",
      "retrig", "delay", "cut"]
NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
PC = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
OFF = 128
LIMITS = {"sounds": 32, "sfx": 64, "patterns": 64, "songs": 8, "samples": 64}
# samples (src/audio/player.h, synth.h)
FORMATS = ["u8", "s16", "s24", "s32", "f32"]
SIZES = [1, 2, 3, 4, 4]
LOOPS = ["off", "fwd", "pingpong"]
PCM_MAX = 1 << 20           # 16-bit values of a bank's samples (AU_PCM_MAX)
GUARD = 4                   # frames the console adds to each (SYNTH_SAMPLE_GUARD)
KIT = ["bd", "sd", "hh", "oh", "cp", "rim", "tom", "cb"]       # MOD1 128.. (SYNTH_KIT)
SAMPLE_WAVE = WAVES.index("sample")


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


def sample_ref(v, samples):
    """a sound's "sample": a number, a name of the bank's samples, a kit name or "kit:N" -> MOD1"""
    if isinstance(v, int):
        if not 0 <= v <= 255:
            raise ValueError(f"sample out of range: {v}")
        return v
    v = str(v)
    for i, x in enumerate(samples):
        if x.get("name", "").lower() == v.lower():
            return i
    if v.lower() in KIT:
        return 128 + KIT.index(v.lower())
    if v.lower().startswith("kit:") and v[4:].isdigit():
        return 128 + int(v[4:]) % len(KIT)
    raise ValueError(f"no sample called {v!r}")


def tone_bytes(s, samples=()):
    """a sound's tone keys -> the 21 register bytes"""
    t = bytearray(TONE)
    t[11] = ROOM_SEND
    for key, i, signed in TONE_KEYS:
        if key in s:
            v = int(s[key])
            if not (-128 <= v <= 127 if signed else 0 <= v <= 255):
                raise ValueError(f"{key} out of range: {v}")
            t[i] = v & 255
    def pick(key, names):
        v = s.get(key, 0)
        return names.index(v) if isinstance(v, str) else int(v)
    f = s.get("filter", "lp")
    t[2] = (FILTERS.index(f) if isinstance(f, str) else int(f) & 3) | (4 if s.get("keytrack") else 0)
    t[2] |= (pick("vowel", VOWELS) & 7) << 3 | (int(s.get("filter_bits", 0)) & 0xC0)
    lfo = s.get("lfo", [0, 0, 0])
    t[13:16] = bytes(int(x) for x in lfo[:3])
    if "sample" in s:
        t[6] = sample_ref(s["sample"], samples)
    if "begin" in s:
        t[7] = int(s["begin"]) & 255
    if "density" in s:
        t[6] = int(s["density"]) & 255
    t[17] = (int(s.get("flags", 0)) & 0x80) | (FLAG_RAW if s.get("raw") else 0) | (FLAG_REVERSE if s.get("reverse") else 0)
    t[17] |= (pick("color", COLORS) & 3) << 1 | (pick("curve", CURVES) & 7) << 3
    crush, coarse = int(s.get("crush", 0)), int(s.get("coarse", 1))
    trem, duck, chorus = int(s.get("trem", 0)), int(s.get("duck", 0)), int(s.get("chorus", 0))
    if not (0 <= crush <= 15 and 1 <= coarse <= 16 and 0 <= trem <= 15 and 0 <= duck <= 15 and 0 <= chorus <= 255):
        raise ValueError(f"crush 0..15, coarse 1..16, trem and duck 0..15, chorus 0..255: {s.get('name', '')!r}")
    t[18] = crush | (coarse - 1) << 4
    t[19] = trem | duck << 4
    t[20] = chorus
    return bytes(t)


def tone_keys(t, wave=None, names=()):
    """the 21 register bytes -> the keys that are not the plain tone"""
    out = {}
    sample = wave == "sample"
    for key, i, signed in TONE_KEYS:
        if sample and key in ("mod1", "mod2"):
            continue
        v = t[i] - 256 if signed and t[i] > 127 else t[i]
        if v != (ROOM_SEND if key == "reverb" else 0):
            out[key] = v
    if t[2] & 3:
        out["filter"] = FILTERS[t[2] & 3]
    if t[2] & 4:
        out["keytrack"] = True
    if t[2] >> 3 & 7:
        out["vowel"] = VOWELS[t[2] >> 3 & 7] if (t[2] >> 3 & 7) < len(VOWELS) else t[2] >> 3 & 7
    if t[2] & 0xC0:
        out["filter_bits"] = t[2] & 0xC0        # not used (yet): kept as they are
    if any(t[13:16]):
        out["lfo"] = list(t[13:16])
    if sample:
        m = t[6]
        if m >= 128:
            out["sample"] = KIT[m - 128] if m - 128 < len(KIT) else m
        elif m < len(names) and names[m] and [n.lower() for n in names].count(names[m].lower()) == 1:
            out["sample"] = names[m]            # by name, when that is not ambiguous
        else:
            out["sample"] = m
        if t[7]:
            out["begin"] = t[7]
    if t[17] & FLAG_RAW:
        out["raw"] = True
    if t[17] & FLAG_REVERSE:
        out["reverse"] = True
    if t[17] >> 1 & 3:
        out["color"] = COLORS[t[17] >> 1 & 3]
    if t[17] >> 3 & 7:
        out["curve"] = CURVES[t[17] >> 3 & 7] if (t[17] >> 3 & 7) < len(CURVES) else t[17] >> 3 & 7
    if t[17] & 0x80:
        out["flags"] = 0x80
    if t[18] & 15:
        out["crush"] = t[18] & 15
    if t[18] >> 4:
        out["coarse"] = (t[18] >> 4) + 1
    if t[19] & 15:
        out["trem"] = t[19] & 15
    if t[19] >> 4:
        out["duck"] = t[19] >> 4
    if t[20]:
        out["chorus"] = t[20]
    return out


# ---------------------------------------------------------------- samples

def read_wav(data):
    """a WAV file's bytes -> (format, channels, rate, frames bytes, root, loop) with format one of
    FORMATS (64-bit floats become 32-bit); root and loop (mode, start, end) from its smpl chunk or None"""
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("not a WAV file")
    pos, fmt, pcm, smpl = 12, None, None, None
    while pos + 8 <= len(data):
        cid = data[pos:pos + 4]
        size = struct.unpack_from("<I", data, pos + 4)[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            tag, ch, rate, _, _, bits = struct.unpack_from("<HHIIHH", body)
            if tag == 0xFFFE and len(body) >= 26:
                tag = struct.unpack_from("<H", body, 24)[0]         # WAVE_FORMAT_EXTENSIBLE: its sub-format
            fmt = (tag, ch, rate, bits)
        elif cid == b"data":
            pcm = body
        elif cid == b"smpl" and len(body) >= 36:
            smpl = body
        pos += 8 + size + (size & 1)
    if fmt is None or pcm is None:
        raise ValueError("a WAV file without its format or its data")
    tag, ch, rate, bits = fmt
    kinds = {(1, 8): "u8", (1, 16): "s16", (1, 24): "s24", (1, 32): "s32", (3, 32): "f32", (3, 64): "f64"}
    if (tag, bits) not in kinds:
        raise ValueError(f"WAV samples of {bits} bits (format {tag}) are not read: 8, 16, 24, 32-bit or float")
    if ch not in (1, 2):
        raise ValueError(f"a WAV of {ch} channels: mono or stereo")
    form = kinds[(tag, bits)]
    if form == "f64":
        n = len(pcm) // 8
        pcm = struct.pack(f"<{n}f", *struct.unpack_from(f"<{n}d", pcm))
        form = "f32"
    size = SIZES[FORMATS.index(form)]
    pcm = pcm[:len(pcm) // (size * ch) * size * ch]
    root, loop = None, None
    if smpl:
        root = struct.unpack_from("<I", smpl, 12)[0]
        root = root if 1 <= root <= 127 else None
        if struct.unpack_from("<I", smpl, 28)[0] >= 1 and len(smpl) >= 60:
            _, kind, start, end = struct.unpack_from("<IIII", smpl, 36)
            loop = ("pingpong" if kind == 1 else "fwd", start, end + 1)    # the smpl end is inclusive
    return form, ch, rate, bytes(pcm), root, loop


def decode(pcm, form):
    """frames' bytes -> floats, 1.0 full scale"""
    n = len(pcm) // SIZES[FORMATS.index(form)]
    if form == "u8":
        return [(b - 128) / 128.0 for b in pcm]
    if form == "s16":
        return [v / 32768.0 for v in struct.unpack(f"<{n}h", pcm)]
    if form == "s24":
        return [(int.from_bytes(pcm[3 * i:3 * i + 3], "little", signed=True)) / 8388608.0 for i in range(n)]
    if form == "s32":
        return [v / 2147483648.0 for v in struct.unpack(f"<{n}i", pcm)]
    return [v if v == v else 0.0 for v in struct.unpack(f"<{n}f", pcm)]


def encode(values, form):
    """floats -> frames' bytes in a format (rounded, clamped)"""
    def q(v, top):
        x = math.floor(v * top + 0.5)
        return max(-top, min(top - 1, x))
    if form == "u8":
        return bytes(q(v, 128) + 128 for v in values)
    if form == "s16":
        return struct.pack(f"<{len(values)}h", *(q(v, 32768) for v in values))
    if form == "s24":
        return b"".join(q(v, 8388608).to_bytes(3, "little", signed=True) for v in values)
    if form == "s32":
        return struct.pack(f"<{len(values)}i", *(q(v, 2147483648) for v in values))
    return struct.pack(f"<{len(values)}f", *values)


def to_note(v, default=60):
    if v is None:
        return default
    return parse_note(v) if isinstance(v, str) else int(v)


def sample_bytes(x, base="."):
    """a sample of the JSON -> its 32-byte header and frames"""
    if "wav" in x:
        form, ch, rate, pcm, root, loop = read_wav(open(os.path.join(base, x["wav"]), "rb").read())
    else:
        form, ch, rate = x.get("format", "s16"), int(x.get("channels", 1)), int(x.get("rate", 48000))
        pcm, root, loop = base64.b64decode(x["pcm"]), None, None
        if form not in FORMATS:
            raise ValueError(f"sample format {form!r}: one of {FORMATS}")
    store = x.get("store", form)
    if store not in FORMATS:
        raise ValueError(f"sample store {store!r}: one of {FORMATS}")
    if (x.get("mono") and ch == 2) or store != form:
        v = decode(pcm, form)
        if x.get("mono") and ch == 2:
            v = [(v[i] + v[i + 1]) * 0.5 for i in range(0, len(v) - 1, 2)]
            ch = 1
        pcm, form = encode(v, store), store
    frames = len(pcm) // (SIZES[FORMATS.index(form)] * ch)
    if ch not in (1, 2) or frames < 1:
        raise ValueError(f"sample {x.get('name', '')!r}: no frames")
    if not 1000 <= rate <= 192000:
        raise ValueError(f"sample {x.get('name', '')!r}: a rate of {rate} Hz (1000..192000)")
    mode = x.get("loop", loop[0] if loop else "off")
    mode = LOOPS.index(mode) if isinstance(mode, str) else int(mode)
    ls = int(x.get("loop_start", loop[1] if loop else 0))
    le = int(x.get("loop_end", loop[2] if loop else 0))
    root = to_note(x.get("root"), root or 60)
    fine = int(x.get("fine", 0))
    if not (1 <= root <= 127 and -100 <= fine <= 100 and 0 <= mode < len(LOOPS)):
        raise ValueError(f"sample {x.get('name', '')!r}: bad root, fine tune or loop")
    head = name8(x.get("name", "")) + struct.pack("<IIBBBbB3xII", frames, rate, FORMATS.index(form), ch, root,
                                                  fine, mode, ls, le)
    return head + pcm, (frames + GUARD) * ch


def pack(bank, base="."):
    """the JSON dict -> the AUDIO section bytes (WAV files from base)"""
    sounds, sfx = bank.get("sounds", []), bank.get("sfx", [])
    pats, songs = bank.get("patterns", []), bank.get("songs", [])
    samples = bank.get("samples", [])
    for k, v in (("sounds", sounds), ("sfx", sfx), ("patterns", pats), ("songs", songs), ("samples", samples)):
        if len(v) > LIMITS[k]:
            raise ValueError(f"at most {LIMITS[k]} {k}")
    version = VERSION if samples else VERSION_PLAIN
    out = bytearray(MAGIC + bytes([version, len(sounds), len(sfx), len(pats), len(songs), len(samples)]) +
                    b"\0" * 6)
    for s in sounds:
        wave = s.get("wave", "square")
        wave = WAVES.index(wave) if isinstance(wave, str) else int(wave)
        a, d, su, r = s.get("adsr", [1, 0, 255, 10])
        vd, vr = s.get("vibrato", [0, 0])
        out += name8(s.get("name", ""))
        out += struct.pack("<7BbBBBb4x", wave, s.get("duty", 128), s.get("vol", 200), a, d, su, r,
                           s.get("pitch", 0), s.get("pitch_time", 0), vd, vr, s.get("detune", 0))
        out += tone_bytes(s, samples) + b"\0" * 3
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
                                                 255 if loop is None else loop, s.get("echo", 0),
                                                 s.get("room", 0), 0, 0]) + bytes(order)
    need = 0
    for x in samples:
        b, n = sample_bytes(x, base)
        need += n
        out += b
    if need > PCM_MAX:
        raise ValueError(f"the samples need {need} values of memory: at most {PCM_MAX} "
                         f"({PCM_MAX // 48000} s of mono at 48 kHz)")
    return bytes(out)


def unpack(data):
    """the AUDIO section bytes -> the JSON dict (samples with their frames in base64)"""
    if data[:4] != MAGIC or data[4] not in (1, 2, 3):
        raise ValueError("not a version 1, 2 or 3 sound bank")
    version = data[4]
    ns, nx, np_, ng = data[5:9]
    nsm = data[9] if version >= 3 else 0
    off = 16
    name = lambda b: b.split(b"\0")[0].decode("ascii", "replace")
    bank = {"sounds": [], "sfx": [], "patterns": [], "songs": []}
    tones = []
    for _ in range(ns):
        f = struct.unpack_from("<8s7BbBBBb4x", data, off)
        snd = {"name": name(f[0]), "wave": WAVES[f[1]] if f[1] < len(WAVES) else f[1],
               "duty": f[2], "vol": f[3], "adsr": list(f[4:8]), "pitch": f[8],
               "pitch_time": f[9], "vibrato": [f[10], f[11]], "detune": f[12]}
        tones.append(data[off + 24:off + 24 + TONE] if version >= 2 else None)
        off += 24 if version == 1 else 48
        bank["sounds"].append(snd)
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
        nm, bpm, swing, n, loop, echo, room = struct.unpack_from("<8s6B", data, off)
        off += 16
        song = {"name": name(nm), "bpm": bpm, "swing": swing, "loop": None if loop == 255 else loop}
        if version >= 2 and echo:
            song["echo"] = echo
        if version >= 2 and room:
            song["room"] = room
        song["order"] = list(data[off:off + n])
        bank["songs"].append(song)
        off += n
    samples = []
    for _ in range(nsm):
        nm, frames, rate, form, ch, root, fine, mode, ls, le = struct.unpack_from("<8sIIBBBbB3xII", data, off)
        off += 32
        size = frames * ch * SIZES[form]
        x = {"name": name(nm), "format": FORMATS[form], "channels": ch, "rate": rate, "root": root or 60}
        if fine:
            x["fine"] = fine
        if mode:
            x["loop"] = LOOPS[mode] if mode < len(LOOPS) else mode
            x["loop_start"], x["loop_end"] = ls, le
        x["pcm"] = base64.b64encode(data[off:off + size]).decode("ascii")
        off += size
        samples.append(x)
    names = [x["name"] for x in samples]
    for snd, t in zip(bank["sounds"], tones):
        if t is not None:
            snd.update(tone_keys(t, snd["wave"], names))
    if samples:
        bank["samples"] = samples
    return bank


def write_wav(path, x):
    """a sample of unpack()'s dict -> a WAV file in its own format (the root and loop in a smpl chunk)"""
    pcm = base64.b64decode(x["pcm"])
    form, ch, rate = x["format"], x["channels"], x["rate"]
    size = SIZES[FORMATS.index(form)]
    tag = 3 if form == "f32" else 1
    fmt = struct.pack("<HHIIHH", tag, ch, rate, rate * ch * size, ch * size, size * 8)
    body = b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt
    root, loop = x.get("root", 60), x.get("loop", "off")
    if root != 60 or loop != "off":
        loops = []
        if loop != "off":
            end = x.get("loop_end", 0) or len(pcm) // (size * ch)
            loops = [struct.pack("<IIIIII", 0, 1 if loop == "pingpong" else 0, x.get("loop_start", 0), end - 1, 0, 0)]
        smpl = struct.pack("<IIIIIIIII", 0, 0, round(1e9 / rate), root, 0, 0, 0, len(loops), 0) + b"".join(loops)
        body += b"smpl" + struct.pack("<I", len(smpl)) + smpl
    body += b"data" + struct.pack("<I", len(pcm)) + pcm + (b"\0" if len(pcm) & 1 else b"")
    open(path, "wb").write(b"RIFF" + struct.pack("<I", len(body)) + body)


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
    """a bank from .json (its WAV files beside it), .bmau or .bm -> section bytes"""
    data = open(path, "rb").read()
    if path.lower().endswith(".json"):
        return pack(json.loads(data), os.path.dirname(os.path.abspath(path)))
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
        bank = unpack(data)
        if a.output:
            # the samples as WAV files beside the JSON (its name, a number, theirs)
            stem = os.path.splitext(a.output)[0]
            for i, x in enumerate(bank.get("samples", [])):
                wav = f"{stem}.{i}.{''.join(c for c in x['name'] if c.isalnum() or c in '-_') or 'sample'}.wav"
                write_wav(wav, x)       # in the format it is stored in: packed again, the same bytes
                for k in ("pcm", "format", "channels", "rate"):
                    del x[k]
                x["wav"] = os.path.basename(wav)
            open(a.output, "w").write(dumps(bank))
        else:
            print(dumps(bank))
    else:
        b = unpack(data)
        print(f"{len(data)} bytes: {len(b['sounds'])} sounds, {len(b['sfx'])} sound effects, "
              f"{len(b['patterns'])} patterns, {len(b['songs'])} songs, {len(b.get('samples', []))} samples")
        for i, x in enumerate(b["sounds"]):
            print(f"  sound  {i:2} {x['name']:8} {x['wave']}" + (f" {x['sample']}" if "sample" in x else ""))
        for i, x in enumerate(b["sfx"]):
            print(f"  sfx    {i:2} {x['name']:8} {len(x['steps'])} steps of {x['ms']} ms")
        for i, x in enumerate(b["songs"]):
            print(f"  song   {i:2} {x['name']:8} {x['bpm']} BPM, patterns {x['order']}")
        for i, x in enumerate(b.get("samples", [])):
            frames = len(base64.b64decode(x["pcm"])) // (SIZES[FORMATS.index(x["format"])] * x["channels"])
            print(f"  sample {i:2} {x['name']:8} {frames / x['rate']:.2f} s, {x['format']} "
                  f"{'stereo' if x['channels'] == 2 else 'mono'} {x['rate']} Hz, root {note_name(x['root'])}"
                  + (f", loop {x['loop']} {x['loop_start']}..{x['loop_end']}" if "loop" in x else ""))


if __name__ == "__main__":
    sys.exit(main())
