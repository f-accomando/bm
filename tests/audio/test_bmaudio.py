#!/usr/bin/env python3
"""Samples in the sound banks, on the PC (make test-audio):

  test_bmaudio.py BMRENDER OUTDIR

scripts/bmaudio.py reads WAV files of every format (8-bit, 16, 24, 32-bit
integers, 32 and 64-bit floats, mono and stereo, with or without a smpl
chunk), packs them in a bank of version 3 (a bank without samples stays
version 2), unpacks them (in base64, or as WAV files beside the JSON) and
packs them back byte for byte; the console's own parser and synthesizer
(BMRENDER, tests/audio/render.c: au_parse_pcm, the SAMPLE wave) read the
same bank and play every sample at the pitch, in the channels and for the
time it should."""
import json
import math
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "scripts"))
import bmaudio  # noqa: E402

checks = fails = 0


def check(ok, what):
    global checks, fails
    checks += 1
    if not ok:
        fails += 1
        print("FAIL", what)


def wav_bytes(form, ch, rate, frames, extensible=False, smpl=None):
    """a WAV file of `frames` (a list of tuples, one value per channel, 1.0 full scale)"""
    flat = [v for f in frames for v in f]
    if form == "f64":
        pcm, tag, bits = struct.pack(f"<{len(flat)}d", *flat), 3, 64
    else:
        pcm = bmaudio.encode(flat, form)
        tag, bits = (3, 32) if form == "f32" else (1, 8 * bmaudio.SIZES[bmaudio.FORMATS.index(form)])
    size = bits // 8
    if extensible:
        guid = struct.pack("<H", tag) + b"\x00\x00\x00\x00\x10\x00\x80\x00\x00\xaa\x00\x38\x9b\x71"
        fmt = struct.pack("<HHIIHHHHI", 0xFFFE, ch, rate, rate * ch * size, ch * size, bits, 22, bits, 3) + guid
    else:
        fmt = struct.pack("<HHIIHH", tag, ch, rate, rate * ch * size, ch * size, bits)
    body = b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt
    if smpl:
        root, kind, start, end = smpl
        s = struct.pack("<IIIIIIIII", 0, 0, 0, root, 0, 0, 0, 1, 0) + struct.pack("<IIIIII", 0, kind, start, end, 0, 0)
        body += b"LIST" + struct.pack("<I", 4) + b"INFO"           # a chunk to skip
        body += b"smpl" + struct.pack("<I", len(s)) + s
    body += b"data" + struct.pack("<I", len(pcm)) + pcm + (b"\0" if len(pcm) & 1 else b"")
    return b"RIFF" + struct.pack("<I", len(body)) + body


def sine(hz, rate, n, amp=0.5):
    return [amp * math.sin(2 * math.pi * hz * i / rate) for i in range(n)]


def read_out(path):
    """bmrender's WAV: 16-bit stereo at 48 kHz -> left, right"""
    data = open(path, "rb").read()
    pcm = data[44:]
    v = struct.unpack(f"<{len(pcm) // 2}h", pcm)
    return v[0::2], v[1::2]


def crossings_hz(x, a, b):
    up = sum(1 for i in range(a + 1, b) if x[i - 1] < 0 <= x[i])
    return up * 48000.0 / (b - a)


def peak(x, a, b):
    return max(abs(v) for v in x[a:b]) if b > a else 0


def main():
    render, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)

    # ---- the formats: a 441 Hz sine (one second at 22050 Hz) in each
    forms = ["u8", "s16", "s24", "s32", "f32", "f64"]
    samples, sounds, sfx = [], [], []
    for i, form in enumerate(forms):
        path = os.path.join(out, f"sine_{form}.wav")
        open(path, "wb").write(wav_bytes(form, 1, 22050, [(v,) for v in sine(441, 22050, 22050)],
                                         extensible=form == "s24"))
        samples.append({"name": f"SIN{form.upper()}", "wav": os.path.basename(path)})
    # stereo: 441 Hz on the left, 662 on the right, 44.1 kHz
    path = os.path.join(out, "stereo.wav")
    st = list(zip(sine(441, 44100, 44100), sine(662, 44100, 44100)))
    open(path, "wb").write(wav_bytes("s16", 2, 44100, st))
    samples.append({"name": "STEREO", "wav": "stereo.wav"})
    # a loop of one cycle (100 frames of 441 Hz at 44.1 kHz), root A4 and the loop from smpl
    path = os.path.join(out, "cycle.wav")
    open(path, "wb").write(wav_bytes("s16", 1, 44100, [(v,) for v in sine(441, 44100, 100, 0.6)],
                                     smpl=(69, 0, 0, 99)))
    samples.append({"name": "CYCLE", "wav": "cycle.wav"})
    # the same as a ping-pong loop and stored as 8 bits; a stereo file mixed to mono
    samples.append({"name": "PING", "wav": "cycle.wav", "loop": "pingpong", "loop_start": 25, "loop_end": 75,
                    "store": "u8", "root": "A4"})
    samples.append({"name": "MONO", "wav": "stereo.wav", "mono": True})
    for i, x in enumerate(samples):
        sounds.append({"name": x["name"], "wave": "sample", "sample": x["name"], "vol": 255,
                       "adsr": [0, 0, 255, 2], "reverb": 0})
        sfx.append({"name": x["name"], "ms": 1500, "steps": [f"C4 {i} 255"]})
    sounds.append({"name": "KICK", "wave": "sample", "sample": "bd", "vol": 255, "adsr": [0, 0, 255, 2],
                   "reverb": 0, "begin": 64, "reverse": True})
    octave, kick = len(sfx), len(sfx) + 1
    sfx.append({"name": "OCTAVE", "ms": 1500, "steps": ["C5 0 255"]})
    sfx.append({"name": "KICK", "ms": 1000, "steps": [f"C4 {len(samples)} 255"]})
    bank = {"sounds": sounds, "sfx": sfx, "samples": samples}
    json.dump(bank, open(os.path.join(out, "bank.json"), "w"))
    data = bmaudio.load(os.path.join(out, "bank.json"))
    check(data[:4] == b"BMAU" and data[4] == 3 and data[9] == len(samples), "version 3, the samples counted")

    # ---- unpack and pack again, byte for byte: base64, and WAV files beside the JSON
    b = bmaudio.unpack(data)
    check(bmaudio.pack(b) == data, "unpack -> pack (base64): the same bytes")
    check([x["format"] for x in b["samples"][:6]] == ["u8", "s16", "s24", "s32", "f32", "f32"],
          "each format kept (64-bit floats as 32)")
    check(b["samples"][7]["root"] == 69 and b["samples"][7]["loop"] == "fwd" and
          b["samples"][7]["loop_end"] == 100, "the smpl chunk: root A4, a loop of 100 frames")
    check(b["samples"][8]["format"] == "u8" and b["samples"][8]["loop"] == "pingpong", "stored as asked")
    check(b["samples"][9]["channels"] == 1 and b["samples"][6]["channels"] == 2, "a stereo file made mono")
    check(b["sounds"][0]["sample"] == "SINU8" and b["sounds"][-1]["sample"] == "bd" and
          b["sounds"][-1]["begin"] == 64 and b["sounds"][-1]["reverse"], "the sounds name their samples")
    bmau = os.path.join(out, "bank.bmau")
    open(bmau, "wb").write(data)
    js = os.path.join(out, "again.json")
    subprocess.run([sys.executable, os.path.join(HERE, "..", "..", "scripts", "bmaudio.py"), "unpack", bmau,
                    "-o", js], check=True, stdout=subprocess.DEVNULL)
    again = json.load(open(js))
    check(all("wav" in x and "pcm" not in x for x in again["samples"]), "unpack -o: the samples as WAV files")
    check(bmaudio.load(js) == data, "WAV files -> pack: the same bytes")

    # ---- a bank without samples stays version 2; limits
    plain = bmaudio.pack({"sounds": [{"name": "A"}]})
    check(plain[4] == 2 and plain[9] == 0, "no samples: version 2")
    # the effects' keys: into registers 13, 28..31 (src/audio/synth.h) and back
    fx = {"name": "FX", "wave": "pink", "filter": "bp", "keytrack": True, "vowel": "o", "curve": "fold",
          "color": "brown", "raw": True, "crush": 4, "coarse": 8, "trem": 9, "duck": 15, "chorus": 200}
    fb = bmaudio.pack({"sounds": [fx]})
    t = fb[16 + 24:16 + 24 + 21]
    check(fb[16 + 8] == 11 and t[2] == (1 | 4 | 4 << 3) and t[17] == (1 | 2 << 1 | 2 << 3) and
          t[18] == (4 | 7 << 4) and t[19] == (9 | 15 << 4) and t[20] == 200, "the effects in their registers")
    back = bmaudio.unpack(fb)["sounds"][0]
    check(all(back[k] == v for k, v in fx.items()) and bmaudio.pack(bmaudio.unpack(fb)) == fb,
          "the effects' keys back, the same bytes")
    try:
        bmaudio.pack({"samples": [{"name": "BIG", "pcm": "AAAA" * 700000, "format": "s16", "rate": 48000}]})
        check(False, "too long: refused")
    except ValueError:
        check(True, "")
    for bad in (b"RIFX", wav_bytes("s16", 1, 8000, [(0.0,)])[:30]):
        try:
            bmaudio.read_wav(bad)
            check(False, "a broken WAV refused")
        except (ValueError, struct.error):
            check(True, "")

    # ---- the console reads and plays them (au_parse_pcm, the SAMPLE wave)
    res = subprocess.run([render, bmau, os.path.join(out, "x.wav"), "sfx", "0", "0.1"], capture_output=True,
                         text=True)
    lines = [l for l in res.stdout.splitlines() if l.startswith("sample")]
    check(res.returncode == 0 and len(lines) == len(samples), "bmrender reads the samples")
    check(any("CYCLE: 100 frames, 44100 Hz, 1 channels, root 69, loop 1 [0, 100)" in l for l in lines),
          "the C parser agrees: " + (lines[7] if len(lines) > 7 else ""))
    check(any("PING: 75 frames" in l and "loop 2 [25, 75)" in l for l in lines), "a ping-pong loop to its end")
    for i in range(len(samples)):
        wav = os.path.join(out, f"play{i}.wav")
        subprocess.run([render, bmau, wav, "sfx", str(i), "1.6"], check=True, stdout=subprocess.DEVNULL)
        left, right = read_out(wav)
        name = samples[i]["name"]
        if i < 6:       # one second of 441 Hz, then silence
            hz = crossings_hz(left, 4800, 43200)
            check(abs(hz - 441) < 3, f"{name}: 441 Hz at its root ({hz:.1f})")
            check(peak(left, 4800, 43200) > 9000, f"{name}: as loud as the others")
            check(peak(left, 52000, 72000) < 40, f"{name}: a one-shot ends with its frames")
        elif name == "STEREO":
            hl, hr = crossings_hz(left, 4800, 43200), crossings_hz(right, 4800, 43200)
            check(abs(hl - 441) < 3 and abs(hr - 662) < 3, f"stereo: 441 Hz left, 662 right ({hl:.1f}, {hr:.1f})")
        elif name in ("CYCLE", "PING"):
            # root A4 played at C4: one cycle of 441 Hz is 262.2 Hz; the ping-pong of half a cycle,
            # 49 frames each way, 267.6 Hz
            hz = crossings_hz(left, 4800, 67200)
            want = 441 * 2 ** (-9 / 12) * (1 if name == "CYCLE" else 100 / 98)
            check(abs(hz - want) < 3, f"{name}: the loop goes on at {want:.1f} Hz ({hz:.1f})")
            check(peak(left, 60000, 67200) > 5000, f"{name}: still sounding after 1.4 s")
        elif name == "MONO":
            check(max(abs(left[k] - right[k]) for k in range(4800, 43200)) <= 2 and peak(left, 4800, 43200) > 5000,
                  "a stereo file made mono: the same both sides")
    # the octave: C5 plays the first sample twice as fast (882 Hz, half a second)
    wav = os.path.join(out, "octave.wav")
    subprocess.run([render, bmau, wav, "sfx", str(octave), "1.6"], check=True, stdout=subprocess.DEVNULL)
    left, _ = read_out(wav)
    hz = crossings_hz(left, 2400, 21600)
    check(abs(hz - 882) < 4, f"an octave up: 882 Hz ({hz:.1f})")
    check(peak(left, 30000, 40000) < 40, "an octave up: over in half the time")
    # the kit's kick, backwards from three quarters of it
    wav = os.path.join(out, "kick.wav")
    subprocess.run([render, bmau, wav, "sfx", str(kick), "1.0"], check=True, stdout=subprocess.DEVNULL)
    left, _ = read_out(wav)
    check(peak(left, 0, 4800) > 1000 and peak(left, 24000, 48000) < 40, "the kit's kick backwards, then over")

    print(f"bmaudio: {checks - fails}/{checks} checks passed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
