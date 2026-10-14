#!/usr/bin/env python3
"""Tropic Trail, video 5: checks what bm Sound saved in the recording, with no pictures.

  python3 video/3d/05-sound/verify.py [OUT_DIR] [--start forge|chain]

Reads OUT_DIR/sd/carts/TROPIC.BME (OUT_DIR default: video/3d/05-sound/out; what the editor saved), the cartridge of the chain
(video/3d/chain/05-sound.bm / .bme, when record.py put them there), OUT_DIR/sound.wav (what the console played) and
OUT_DIR/events.json (the frames where record.py plays an effect or the song). The answers are the recipes of
docs/video/05-sound/script.md, written here again as raw editor values (not taken from record.py).

  1. the sound bank (scripts/bmaudio.py): sounds 0-10 APPLE CRATE JUMP STEP SPIN HIT KICK SHAKER BASS MARIMBA PAD with their
     values; effects 0-5 (name, SPEED, notes); song 0 JUNGLE (BPM, swing, echo, room, loop, the 24 slots); patterns 0-2 (tracks
     1-6 as in the tables; tracks 7 and 8 empty in every pattern: the two voices the effects borrow)
  2. the cartridge: the other sections (MESH, ANIM, SHEET ...) are byte for byte those of the start cartridge; chain/05-sound.bm
     is a cartridge (project flag off) with the same bank
  3. the wav: silent before anything plays, a peak in every effect window, the song never silent or interrupted
  4. THE VOLUME RULE: peak <= -6 dBFS; RMS of every effect and of the music below -18 dBFS; the music about 6 dB under the
     effects (3 to 9 dB). Effect RMS = 0.5 s from the key that plays it; music RMS = the whole time the song plays.
     The wav is STEREO (pan is audible: MARIMBA left, PAD right), levels are measured on both channels together.
     Nothing is raised or changed: when the editor's values are too loud, verify.py prints the VOLUME (raw 0-255, `_` = -16)
     that would fit, as a proposal, to apply by hand in the script.

Exit code: 0 all fine; 1 something of 1-3 is wrong; 2 only the volume rule (4) is wrong (the proposal is printed).
"""
import array
import hashlib
import json
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import bmaudio  # noqa: E402

args = [a for a in sys.argv[1:] if not a.startswith("--")]
start_opt = sys.argv[sys.argv.index("--start") + 1] if "--start" in sys.argv else None
if start_opt in args:
    args.remove(start_opt)
OUT = args[0] if args else os.path.join(HERE, "out")
BME = os.path.join(OUT, "sd", "carts", "TROPIC.BME")
WAV = os.path.join(OUT, "sound.wav")
CHAIN = os.path.join(ROOT, "video", "3d", "chain")
PEAK_MAX_DB, RMS_MAX_DB = -6.0, -18.0
MUSIC_UNDER = (3.0, 9.0)            # music about 6 dB under the effects
bad = {"data": 0, "volume": 0}


def report(name, ok, detail="", kind="data"):
    print("%-44s %s %s" % (name, "ok" if ok else "WRONG", "" if ok else detail))
    if not ok:
        bad[kind] += 1


# ------------------------------------------------------------------------------------------------ the answers
SOUNDS = [   # name, wave, vol, adsr, then the tone keys that are not 0 (reverb 40 when absent)
    ("APPLE", "fm", 56, [1, 40, 0, 30], dict(mod1=32, moddecay=20, reverb=104, echo=64)),
    ("CRATE", "noise", 72, [1, 20, 0, 10], dict(cutoff=132, resonance=96, filter="bp", fenv=-48, fdecay=20, drive=32, reverb=72)),
    ("JUMP", "sine", 24, [1, 0, 255, 20], dict(pitch=-12, pitch_time=10, reverb=56)),
    ("STEP", "brown", 120, [1, 10, 0, 10], dict(cutoff=96)),
    ("SPIN", "pink", 104, [21, 0, 255, 30], dict(cutoff=144, resonance=80, filter="bp", fenv=64, fdecay=50, reverb=72, echo=48)),
    ("HIT", "triangle", 104, [1, 30, 0, 20], dict(pitch=12, pitch_time=20, cutoff=132, noise=32, reverb=72)),
    ("KICK", "sine", 8, [1, 30, 0, 10], dict(pitch=24, pitch_time=4)),
    ("SHAKER", "noise", 8, [1, 6, 0, 10], dict(cutoff=192, filter="hp", reverb=24)),
    ("BASS", "sine", 8, [2, 0, 255, 30], dict(cutoff=144, drive=32, reverb=24)),
    ("MARIMBA", "pluck", 8, [1, 0, 255, 30], dict(mod1=96, pan=-32, reverb=72, echo=48)),
    ("PAD", "supersaw", 8, [41, 0, 255, 60], dict(cutoff=120, resonance=32, pan=32, reverb=136)),
]
TONE_DEFAULT = dict(pitch=0, pitch_time=0, cutoff=0, resonance=0, filter="lp", fenv=0, fdecay=0, pan=0, mod1=0, mod2=0,
                    moddecay=0, noise=0, drive=0, reverb=40, echo=0, feedback=0)
EFFECTS = [("APPLE", 80, ["E5", "B5"]), ("CRATE", 40, ["C4", "E4", "F4"]), ("JUMP", 50, ["G4", "C5"]),
           ("STEP", 60, ["C4"]), ("SPIN", 80, ["A4"] * 4), ("HIT", 90, ["E3", "C3"])]
SONG = dict(name="JUNGLE", bpm=100, swing=20, loop=0, echo=3, room=128,
            order=[0, 0, 1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 2, 2, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0])
BASE = {0: {1: "C2 6", 7: "C2 6", 9: "C2 6"}, 1: {s: "C2 7" for s in (3, 7, 11, 15)},
        2: {1: "C2 8", 4: "C2 8", 7: "G2 8", 9: "A2 8", 12: "A2 8", 15: "G2 8"}, 4: {1: "E4 10"}}
MELODY = {3: {1: "E5 9", 4: "G5 9", 6: "A5 9", 8: "G5 9", 9: "E5 9", 12: "D5 9", 14: "C5 9", 15: "D5 9"}}


def pattern(n):
    p = {t: dict(v) for t, v in BASE.items()}
    if n >= 1:
        p.update({t: dict(v) for t, v in MELODY.items()})
    if n == 2:
        p[4] = {1: "G4 10"}
        p[5] = {8: "B5", 16: "E5"}
    return p


# ---------------------------------------------------------------------------------------------- the cartridge
def sections(data):
    out = []
    for i in range(data[17]):
        typ, off, size, _ = struct.unpack_from("<IIII", data, 128 + 16 * i)
        out.append((typ, data[off:off + size]))
    return out


def others(data):
    return [hashlib.sha1(b).hexdigest()[:12] for t, b in sections(data) if not b[:4] == bmaudio.MAGIC]


def start_cart():
    chain = os.path.join(CHAIN, "03-animator.bm")
    forge = os.path.join(ROOT, "video", "3d", "out", "assets", "tropic_assets.bm")
    pick = start_opt or ("chain" if os.path.exists(chain) else "forge")
    return chain if pick == "chain" else forge


def check_bank():
    if not os.path.exists(BME):
        report("the editor saved TROPIC.BME", False, BME)
        return None
    data = open(BME, "rb").read()
    sec = bmaudio.extract(data)
    bank = bmaudio.unpack(sec) if sec else None
    report("TROPIC.BME has an AUDIO bank", bank is not None)
    if not bank:
        return None
    report("TROPIC.BME is a project (flag)", data[18] & 1 == 1)
    snd = bank["sounds"]
    report("sound names 0-10", [x["name"] for x in snd[:11]] == [s[0] for s in SOUNDS], [x["name"] for x in snd[:11]])
    for i, (name, wave, vol, adsr, tone) in enumerate(SOUNDS):
        x = snd[i]
        want = dict(TONE_DEFAULT)
        want.update(tone)
        got = {k: x.get(k, TONE_DEFAULT[k]) for k in TONE_DEFAULT}
        got["pitch"], got["pitch_time"] = x["pitch"], x["pitch_time"]
        ok = x["wave"] == wave and x["vol"] == vol and x["adsr"] == adsr and got == want and x["vibrato"] == [0, 0] and x["detune"] == 0
        report("  sound %02d %s" % (i, name), ok, "got %r want wave=%s vol=%d adsr=%r %r" % (
            {k: v for k, v in x.items() if k != "name"}, wave, vol, adsr, tone))
    sfx = bank["sfx"]
    for i, (name, ms, notes) in enumerate(EFFECTS):
        x = sfx[i]
        steps = [t for t in x["steps"] if t != "."]
        want = [(n if i == 0 else "%s %d" % (n, i)) for n in notes]   # sound 0 is not written
        report("  effect %02d %s: %d notes, %d ms" % (i, name, len(notes), ms),
               x["name"] == name and x["ms"] == ms and steps == want and x["loop"] == [0, 0], (x["name"], x["ms"], steps, want))
    s = bank["songs"][0]
    got = {k: s.get(k, 0) for k in SONG}
    report("song 0 JUNGLE: 100 BPM, swing 20, echo 3, room 128", {k: v for k, v in got.items() if k != "order"} == {
        k: v for k, v in SONG.items() if k != "order"}, s)
    report("song 0: the 24 slots", s["order"] == SONG["order"], s["order"])
    for n in range(3):
        p = bank["patterns"][n]
        want = pattern(n)
        got = {}
        for t, steps in p["tracks"].items():
            marks = {k + 1: v for k, v in enumerate(steps) if v != "."}
            if marks:
                got[int(t)] = marks
        report("  pattern %02d: 16 steps, tracks 1-6" % n, p["steps"] == 16 and got == want, (p["steps"], got))
        report("  pattern %02d: tracks 7 and 8 empty" % n, not ({6, 7} & set(got)), set(got))
    return bank


def check_cart():
    start = start_cart()
    if not os.path.exists(start):
        report("start cartridge", False, start + " missing")
        return
    base = others(open(start, "rb").read())
    if os.path.exists(BME):
        report("sections of the start cart kept (BME)", others(open(BME, "rb").read()) == base, "differ from " + os.path.basename(start))
    bm = os.path.join(CHAIN, "05-sound.bm")
    bme = os.path.join(CHAIN, "05-sound.bme")
    for path, project in ((bm, 0), (bme, 1)):
        if not os.path.exists(path):
            report("chain/%s" % os.path.basename(path), False, "missing: run record.py")
            continue
        d = open(path, "rb").read()
        sec = bmaudio.extract(d)
        ok = d[:6] == b"BMCART" and d[18] & 1 == project and sec is not None and others(d) == base
        report("chain/%s: %s, bank + the start's sections" % (os.path.basename(path), "a project" if project else "a cartridge"), ok)
    if os.path.exists(bm) and os.path.exists(BME):
        report("chain/05-sound.bm bank = the saved one", bmaudio.extract(open(bm, "rb").read()) == bmaudio.extract(open(BME, "rb").read()))


# ------------------------------------------------------------------------------------------------------ the wav
def read_wav(path):
    raw = open(path, "rb").read()
    pos, fmt = 12, None
    while pos + 8 <= len(raw):
        cid, sz = raw[pos:pos + 4], struct.unpack_from("<I", raw, pos + 4)[0]
        if cid == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", raw, pos + 8)
        if cid == b"data":
            pcm = array.array("h")
            body = raw[pos + 8:pos + 8 + sz]
            pcm.frombytes(body[:len(body) // 2 * 2])
            if sys.byteorder == "big":
                pcm.byteswap()
            return fmt, pcm
        pos += 8 + sz + (sz & 1)
    raise ValueError("no data chunk")


class Wav:
    def __init__(self, path):
        fmt, pcm = read_wav(path)
        self.ch, self.rate = fmt[1], fmt[2]
        self.pcm = pcm
        self.n = len(pcm) // self.ch

    def seg(self, a, b):
        i, j = max(0, int(a * self.rate)), min(self.n, int(b * self.rate))
        return self.pcm[i * self.ch:j * self.ch]

    def rms(self, a, b):
        s = self.seg(a, b)
        return math.sqrt(sum(v * v for v in s) / max(1, len(s)))

    def peak(self, a=0, b=None):
        s = self.pcm if b is None else self.seg(a, b)
        return max(max(s), -min(s)) if len(s) else 0

    def short_rms_max(self, a, b, win=0.1, hop=0.05):
        t, best = a, 0
        while t + win <= b:
            best = max(best, self.rms(t, t + win))
            t += hop
        return best


def db(v):
    return 20 * math.log10(max(v, 1e-9) / 32768)


def lin(d):
    return 32768 * 10 ** (d / 20)


def check_wav(bank):
    ev_path = os.path.join(OUT, "events.json")
    if not (os.path.exists(WAV) and os.path.exists(ev_path)):
        report("sound.wav and events.json", False, "run record.py")
        return
    w = Wav(WAV)
    ev = json.load(open(ev_path))["events"]
    fps = 60.0
    print("wav: %.1f s, %d channel(s), %d Hz" % (w.n / w.rate, w.ch, w.rate))
    report("silent before anything plays (5 s)", w.rms(0, 5) < 100, w.rms(0, 5))
    fx = [(e["name"], e["kind"], e["frame"] / fps) for e in ev if e["kind"] in ("sfx", "listen")]
    levels = {}
    for name, kind, t in fx:
        pk = w.peak(t, t + 1.2)
        report("  %-6s %-6s at %5.1f s: sound (peak %.1f dB)" % (name, kind, t, db(pk)), pk > 300, pk)
        levels.setdefault(name, []).append((kind, w.rms(t, t + 0.5), pk, w.short_rms_max(t, t + 1.0)))
    song = [e for e in ev if e["kind"] == "song"][0]["frame"] / fps
    stop = [e for e in ev if e["kind"] == "song_stop"][0]["frame"] / fps
    wins = [(song + k * 5, song + k * 5 + 5) for k in range(int((stop - song) // 5))]
    r5 = [w.rms(a, b) for a, b in wins]
    report("the song plays (%d windows of 5 s, rms > 300)" % len(wins), bool(r5) and min(r5) > 300, [round(x) for x in r5])
    quiet = 0
    longest = 0
    t = song
    while t + 0.25 <= stop:
        quiet = quiet + 1 if w.rms(t, t + 0.25) < 50 else 0
        longest = max(longest, quiet)
        t += 0.25
    report("the song has no break of 1 s or more", longest < 4, "%.2f s" % (longest * 0.25))
    pats = [e for e in ev if e["kind"] == "pattern"]
    for e in pats:
        t = e["frame"] / fps
        report("  pattern %s plays" % e["name"], w.peak(t, t + 2.0) > 300, w.peak(t, t + 2.0))
    # --- the volume rule
    pk = w.peak()
    report("peak <= %.0f dBFS (%.1f dBFS)" % (PEAK_MAX_DB, db(pk)), db(pk) <= PEAK_MAX_DB, "%.1f dBFS" % db(pk), "volume")
    fxr = {}
    for name, rows in levels.items():
        fxr[name] = max(r[1] for r in rows)
        report("  effect %-6s RMS 0.5 s < %.0f dBFS (%.1f)" % (name, RMS_MAX_DB, db(fxr[name])), db(fxr[name]) < RMS_MAX_DB,
               "%.1f dBFS" % db(fxr[name]), "volume")
    mus = w.rms(song, stop)
    report("music RMS < %.0f dBFS (%.1f)" % (RMS_MAX_DB, db(mus)), db(mus) < RMS_MAX_DB, "%.1f dBFS" % db(mus), "volume")
    mean_fx = math.sqrt(sum(v * v for v in fxr.values()) / len(fxr))
    gap = db(mean_fx) - db(mus)
    report("music %.0f-%.0f dB under the effects (%.1f dB)" % (MUSIC_UNDER[0], MUSIC_UNDER[1], gap),
           MUSIC_UNDER[0] <= gap <= MUSIC_UNDER[1], "%.1f dB" % gap, "volume")
    if bad["volume"]:
        propose(bank, levels, fxr, mus, pk)


def step16(vol, gain_db):
    new = vol * 10 ** (-gain_db / 20)
    return max(16, int(new // 16 * 16))


def propose(bank, levels, fxr, mus, pk):
    """Proposal only: the VOLUME (raw, step 16 = one `_`) that would bring each sound under the rule. First order: the console's
    compressor and limiter are not linear, so run the recording again and verify.py again after changing the values."""
    print("\nPROPOSAL (nothing is changed): VOLUME of the sounds, raw 0-255 (one `_` = -16, `+` = +16)")
    fx_target, mus_target = RMS_MAX_DB - 1.5, RMS_MAX_DB - 1.5 - 6
    names = {s[0]: (s[2], i) for i, s in enumerate(SOUNDS)}
    for name in ("APPLE", "CRATE", "JUMP", "STEP", "SPIN", "HIT"):
        rows = levels.get(name, [])
        over = max([db(r[1]) - fx_target for r in rows] + [db(r[2]) - (PEAK_MAX_DB - 1.5) for r in rows] + [0])
        vol = names[name][0]
        new = step16(vol, over) if over > 0 else vol
        print("  %-8s VOLUME %3d -> %3d   (%.1f dB over, `_` x%d)" % (name, vol, new, over, (vol - new) // 16))
    over_m = max(db(mus) - mus_target, db(pk) - (PEAK_MAX_DB - 1.5), 0)
    print("  music (tracks 1-6; APPLE is the effect's value) over by %.1f dB: scale every music sound by %.2f" % (
        over_m, 10 ** (-over_m / 20)))
    for name in ("KICK", "SHAKER", "BASS", "MARIMBA", "PAD"):
        vol = names[name][0]
        new = step16(vol, over_m)
        print("  %-8s VOLUME %3d -> %3d   (`_` x%d)" % (name, vol, new, (vol - new) // 16))
    print("  (values at 16 mean the 16-step grid cannot go lower: -18 dB is too much for VOLUME alone; the music also needs fewer"
          " loud tracks or a lower level at the source, to decide)")
    print("  the sums of the sounds' ROOM/ECHO sends and the song's ROOM add to the level: the peak may need one more step.")


def main():
    bank = check_bank()
    check_cart()
    if os.path.exists(WAV):
        check_wav(bank)
    else:
        report("sound.wav", False, "missing: run record.py (not --build-only)")
    print("\nstructure: %s, volume rule: %s" % ("ok" if not bad["data"] else "%d WRONG" % bad["data"],
                                              "ok" if not bad["volume"] else "%d over the limit" % bad["volume"]))
    return 1 if bad["data"] else (2 if bad["volume"] else 0)


if __name__ == "__main__":
    sys.exit(main())
