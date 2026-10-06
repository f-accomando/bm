#!/usr/bin/env python3
"""
Trains the melody network of the music assistant on the PC: `make music-model`.

The tunes of src/ai/melodies.txt (public domain, and written for bm) become
examples of "what comes next": from the last notes (their steps on the
scale and lengths), where in the bar the next one falls, the chord under
it, the place in the phrase and the mode, the next note's step (or a
rest) and its length. The network is small (116 -> 64 -> 24), trained in
floating point, quantized to 8-bit integers (scripts/nnetlib.py, the same
layers as src/ai/nn.h) and written to src/ai/music_net.c, which is
committed: `make` does not need numpy. src/ai/music.c computes the same
features (music_features) and runs it to write melodies.

  trainmusic.py [--epochs N] [--seed S] [--check]

--check: parse the tunes and write build/ai/music_check.txt (the features
and the network's integer outputs for every example), for make test-music.
"""
import argparse
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# ------------------------------------------------------------------ the tunes

DURS = [1, 2, 3, 4, 6, 8, 12, 16]           # lengths in sixteenths: the classes
N_INT = 15                                  # steps -7..+7
REST, NONE = 15, 16
MAJOR = {"I": (0, 0), "ii": (1, 1), "iii": (2, 1), "IV": (3, 0), "V": (4, 0), "vi": (5, 1), "vii": (6, 2)}
MINOR = {"i": (0, 1), "ii": (1, 2), "III": (2, 0), "iv": (3, 1), "v": (4, 1), "V": (4, 0), "VI": (5, 0),
         "VII": (6, 0), "IV": (3, 0)}
NFEAT, NOUT = 116, 24
# chosen on tunes left out (five folds): enough to learn the moves, not the tunes
EPOCHS, DROP, DECAY = 20, 0.2, 3e-3


def dur_class(d):
    return min(range(len(DURS)), key=lambda i: (abs(DURS[i] - d), DURS[i]))


def parse_note(tok):
    m = re.fullmatch(r"([#b]?)([1-7r])([',]*)(?::(\d+))?", tok)
    if not m:
        raise ValueError(f"bad note {tok!r}")
    acc, deg, octs, d = m.groups()
    d = int(d) if d else 4
    if deg == "r":
        return None, d
    o = octs.count("'") - octs.count(",")
    return int(deg) - 1 + 7 * o, d


def parse_tunes(path):
    tunes, cur = [], None
    for n, line in enumerate(open(path, encoding="utf-8"), 1):
        line = line.rstrip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("== "):
            cur = {"id": line[3:].strip(), "notes": [], "chords": [], "line": n}
            tunes.append(cur)
            continue
        k, _, v = line.partition(":")
        v = v.strip()
        if k == "mode":
            cur["minor"] = v == "minor"
        elif k == "meter":
            cur["meter"] = int(v)
        elif k == "chords":
            for bar in v.split("|"):
                cur["chords"].append(bar.split())
        elif k == "notes":
            for bar in v.split("|"):
                if bar.strip():
                    cur["notes"].append([parse_note(t) for t in bar.split()])
        elif k == "title":
            cur["title"] = v
    for t in tunes:
        if len(t["chords"]) != len(t["notes"]):
            raise ValueError(f"{t['id']}: {len(t['chords'])} chords, {len(t['notes'])} bars")
        table = MINOR if t["minor"] else MAJOR
        for b, bar in enumerate(t["notes"]):
            if sum(d for _, d in bar) != t["meter"]:
                raise ValueError(f"{t['id']}: bar {b + 1} lasts {sum(d for _, d in bar)}, not {t['meter']}")
            for c in t["chords"][b]:
                if c not in table:
                    raise ValueError(f"{t['id']}: chord {c!r}")
    return tunes


def chord_at(t, time):
    """(root, quality) of the chord under a time"""
    bar = time // t["meter"]
    names = t["chords"][min(bar, len(t["chords"]) - 1)]
    half = 1 if len(names) > 1 and time % t["meter"] >= t["meter"] // 2 else 0
    table = MINOR if t["minor"] else MAJOR
    return table[names[half]]


def chord_tone(deg, chord):
    root, _ = chord
    return (deg - root) % 7 in (0, 2, 4)


# ------------------------------------------------------------------ the features (as music.c)

def features(hist, pos, meter, chord, bar, last_bar, minor):
    """hist: the events before, oldest first, each (deg or None, dur);
    pos: the next onset in the bar; chord (root, quality); bar: the bar's
    index; last_bar: 1 in the tune's last bar. A list of NFEAT 0/1."""
    f = [0] * NFEAT
    notes_before = [i for i, (d, _) in enumerate(hist) if d is not None]
    # 0..50: what the last three events did (a step from the note before, a rest, nothing)
    for k in range(3):
        i = len(hist) - 1 - k
        if i < 0:
            c = NONE
        elif hist[i][0] is None:
            c = REST
        else:
            prev = [j for j in notes_before if j < i]
            c = NONE if not prev else max(-7, min(7, hist[i][0] - hist[prev[-1]][0])) + 7
        f[k * 17 + c] = 1
    # 51..68: the lengths of the last two
    for k in range(2):
        i = len(hist) - 1 - k
        f[51 + k * 9 + (dur_class(hist[i][1]) if i >= 0 else 8)] = 1
    # 69..79: the last note: its degree and octave
    if notes_before:
        d = hist[notes_before[-1]][0]
        f[69 + d % 7] = 1
        f[76 + max(-1, min(2, d // 7)) + 1] = 1
        # 112: it is a tone of the chord now
        f[112] = 1 if chord_tone(d, chord) else 0
    # 80..95: where in the bar
    f[80 + min(pos, 15)] = 1
    # 96..98: the meter
    f[96 + (0 if meter == 16 else 1 if meter == 12 else 2)] = 1
    # 99..108: the chord: root and quality
    f[99 + chord[0]] = 1
    f[106 + chord[1]] = 1
    # 109..111 and 113..114: the bar in a phrase of four, the last bar
    b = bar % 4
    if b < 3:
        f[109 + b] = 1
    else:
        f[113] = 1
    f[114] = 1 if last_bar else 0
    f[115] = 1 if minor else 0
    return f


def examples(tunes):
    X, Y1, Y2 = [], [], []
    for t in tunes:
        events = [e for bar in t["notes"] for e in bar]
        nbars = len(t["notes"])
        time, hist = 0, []
        for deg, dur in events:
            bar = time // t["meter"]
            x = features(hist, time % t["meter"], t["meter"], chord_at(t, time), bar, bar == nbars - 1, t["minor"])
            last = [d for d, _ in hist if d is not None]
            if deg is None:
                y1 = REST
            else:
                y1 = max(-7, min(7, deg - (last[-1] if last else 0))) + 7
            X.append(x)
            Y1.append(y1)
            Y2.append(dur_class(dur))
            hist.append((deg, dur))
            time += dur
    return X, Y1, Y2


# ------------------------------------------------------------------ training

def train(X, Y1, Y2, epochs, seed, drop=DROP, decay=DECAY):
    import numpy as np
    rng = np.random.default_rng(seed)
    X = np.asarray(X, float)
    Y1, Y2 = np.asarray(Y1), np.asarray(Y2)
    H = 64
    W1 = rng.normal(0, np.sqrt(2 / NFEAT), (H, NFEAT))
    b1 = np.zeros(H)
    W2 = rng.normal(0, np.sqrt(2 / H), (NOUT, H))
    b2 = np.zeros(NOUT)
    params = [W1, b1, W2, b2]
    m = [np.zeros_like(p) for p in params]
    v = [np.zeros_like(p) for p in params]
    step, lr = 0, 3e-3
    # light noise on the inputs: other tunes than these
    for ep in range(epochs):
        idx = rng.permutation(len(X))
        for s0 in range(0, len(idx), 64):
            bi = idx[s0:s0 + 64]
            xb = X[bi]
            off = rng.random(xb.shape) < drop
            xb = np.where(off, 0, xb)
            h = np.maximum(xb @ W1.T + b1, 0)
            o = h @ W2.T + b2
            g = np.zeros_like(o)
            for lo, hi, y in ((0, 16, Y1[bi]), (16, 24, Y2[bi])):
                z = o[:, lo:hi] - o[:, lo:hi].max(1, keepdims=True)
                p = np.exp(z)
                p /= p.sum(1, keepdims=True)
                p[np.arange(len(bi)), y] -= 1
                g[:, lo:hi] = p / len(bi)
            gW2 = g.T @ h + decay * W2
            gb2 = g.sum(0)
            gh = (g @ W2) * (h > 0)
            gW1 = gh.T @ xb + decay * W1
            gb1 = gh.sum(0)
            step += 1
            for k, (p, gk) in enumerate(zip(params, [gW1, gb1, gW2, gb2])):
                m[k] = 0.9 * m[k] + 0.1 * gk
                v[k] = 0.999 * v[k] + 0.001 * gk * gk
                p -= lr * (m[k] / (1 - 0.9 ** step)) / (np.sqrt(v[k] / (1 - 0.999 ** step)) + 1e-8)
    h = np.maximum(X @ W1.T + b1, 0)
    o = h @ W2.T + b2
    acc1 = float(np.mean(o[:, :16].argmax(1) == Y1))
    acc2 = float(np.mean(o[:, 16:].argmax(1) == Y2))
    return [(W1, b1, True), (W2, b2, False)], acc1, acc2


def write_c(blob, path, info):
    lines = ["/* The melody network of the music assistant (src/ai/music.c): written by",
             " * scripts/trainmusic.py from the tunes of src/ai/melodies.txt; do not edit.",
             f" * {info} */",
             '#include "music.h"', "",
             f"const unsigned mus_net_len = {len(blob)};",
             "const uint8_t mus_net[] __attribute__((aligned(4))) = {"]
    for i in range(0, len(blob), 16):
        lines.append("    " + ", ".join(str(b) for b in blob[i:i + 16]) + ",")
    lines.append("};")
    open(path, "w").write("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--epochs", type=int, default=EPOCHS)
    ap.add_argument("--seed", type=int, default=3)
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    tunes = parse_tunes(os.path.join(ROOT, "src/ai/melodies.txt"))
    X, Y1, Y2 = examples(tunes)
    import nnetlib
    if a.check:
        # the features and the integer outputs of the committed network, for the C test
        blob = c_blob(os.path.join(ROOT, "src/ai/music_net.c"))
        q = unpack(blob)
        os.makedirs(os.path.join(ROOT, "build/ai"), exist_ok=True)
        with open(os.path.join(ROOT, "build/ai/music_check.txt"), "w") as f:
            for x in X:
                acc = run_acc(q, x)
                f.write("".join(str(v) for v in x) + " " + " ".join(str(v) for v in acc) + "\n")
        print(f"music: {len(tunes)} tunes, {len(X)} examples written for the check")
        return 0
    print(f"music: {len(tunes)} tunes, {len(X)} examples")
    layers, acc1, acc2 = train(X, Y1, Y2, a.epochs, a.seed)
    q = nnetlib.quantize(layers, X)
    blob = nnetlib.pack(q)
    info = (f"{len(tunes)} tunes, {len(X)} notes; the next step right {acc1 * 100:.0f}%, "
            f"its length {acc2 * 100:.0f}% (on the tunes themselves; about 43% and 65% on tunes left out)")
    write_c(blob, os.path.join(ROOT, "src/ai/music_net.c"), info)
    print("music:", info, f"-> src/ai/music_net.c ({len(blob)} bytes)")
    return 0


# ------------------------------------------------------------------ the committed network, read back

def c_blob(path):
    text = open(path).read()
    body = text[text.index("{", text.index("mus_net[]")) + 1:text.rindex("}")]
    return bytes(int(v) for v in re.findall(r"\d+", body))


def unpack(blob):
    import struct
    import numpy as np
    nl, nin = blob[5], struct.unpack_from("<H", blob, 6)[0]
    in_scale, out_scale = struct.unpack_from("<ff", blob, 8)
    off, layers, width = 16, [], nin
    for _ in range(nl):
        nout, relu, shift, mult = struct.unpack_from("<HBBi", blob, off)
        off += 8
        n4 = (width + 3) & ~3
        bq = np.array(struct.unpack_from(f"<{nout}i", blob, off), np.int64)
        off += 4 * nout
        W = np.frombuffer(blob, np.int8, nout * n4, off).reshape(nout, n4)[:, :width].astype(np.int64)
        off += nout * n4
        layers.append((W, bq, bool(relu), mult, shift))
        width = nout
    return {"in_scale": in_scale, "out_scale": out_scale, "layers": layers}


def run_acc(q, x):
    """the last layer's int32 sums for an input, as music.c computes them"""
    import numpy as np
    v = np.asarray(x, float) * q["in_scale"]
    xq = np.clip(np.where(v < 0, np.ceil(v - 0.5), np.floor(v + 0.5)), -127, 127).astype(np.int64)
    for Wq, bq, relu, mult, shift in q["layers"]:
        acc = Wq @ xq + bq
        acc = ((acc + 2 ** 31) % 2 ** 32) - 2 ** 31
        if relu:
            xq = np.minimum(np.where(acc <= 0, 0, (acc * mult + (1 << (shift - 1))) >> shift), 127)
        else:
            return [int(a) for a in acc]


if __name__ == "__main__":
    sys.exit(main())
