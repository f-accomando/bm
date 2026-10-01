#!/usr/bin/env python3
"""
Trains the development assistant's network (M30) on the PC: `make ai-model`.

Every entry of src/ai/kb/*.txt is a class; its `ask:` questions, title and
name (plus variants with a word left out, typos and synonyms) are the
examples. The network is tiny: hashed words and letter triples (4096) ->
64 hidden -> one output per entry. Trained in floating point, then
quantized to 8-bit integers and written to src/ai/assist.weights, which is
committed: `make` does not need numpy.

  trainassist.py [--epochs N] [--hidden H] [--seed S]

The questions of src/ai/kb/tests.txt are never trained on: their accuracy,
float and integer, is printed at the end.
"""
import argparse
import os
import random
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import assistlib as al  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')

# words that mean the same thing for a question about bm: one is swapped for
# another in some training variants (Italian and English)
SYNONYMS = [
    ['disegnare', 'disegno', 'disegna', 'draw', 'drawing', 'mostrare', 'visualizzare'],
    ['muovere', 'muovo', 'spostare', 'sposto', 'move', 'moving', 'movimento'],
    ['personaggio', 'giocatore', 'player', 'eroe', 'hero', 'omino', 'protagonista'],
    ['nemico', 'nemici', 'enemy', 'enemies', 'mostro', 'monster'],
    ['tasto', 'tasti', 'pulsante', 'bottone', 'button', 'buttons', 'key'],
    ['schermo', 'screen', 'display'],
    ['colore', 'colori', 'color', 'colour', 'tinta'],
    ['suono', 'suoni', 'sound', 'effetto', 'sfx'],
    ['musica', 'music', 'melodia', 'canzone', 'song'],
    ['immagine', 'image', 'picture', 'figura'],
    ['testo', 'text', 'scritta', 'scrivere', 'write'],
    ['numero', 'number', 'valore', 'value'],
    ['creare', 'crea', 'create', 'make', 'generare', 'genera', 'generate'],
    ['velocita', 'speed', 'veloce', 'fast'],
    ['punteggio', 'punti', 'score', 'points'],
    ['salvare', 'salva', 'save', 'memorizzare', 'store'],
    ['caricare', 'carica', 'load', 'leggere', 'read'],
    ['sparare', 'sparo', 'spara', 'shoot', 'shooting', 'fire'],
    ['proiettile', 'proiettili', 'bullet', 'bullets', 'colpo', 'colpi'],
    ['collisione', 'collisioni', 'collision', 'urto', 'scontro', 'toccare', 'hit'],
    ['saltare', 'salto', 'salta', 'jump', 'jumping'],
    ['gravita', 'gravity', 'cadere', 'fall', 'caduta'],
    ['mappa', 'map', 'livello', 'level', 'tilemap'],
    ['tile', 'tessera', 'tessere', 'piastrella', 'cella', 'cell'],
    ['animazione', 'animare', 'animate', 'animation'],
    ['fotogramma', 'frame', 'fotogrammi', 'frames'],
    ['casuale', 'random', 'caso', 'aleatorio'],
    ['tempo', 'time', 'timer', 'secondi', 'seconds'],
    ['errore', 'error', 'sbaglio', 'bug'],
    ['funzione', 'function', 'func'],
    ['tabella', 'table', 'array', 'lista', 'list', 'elenco'],
    ['cerchio', 'circle', 'palla', 'ball'],
    ['rettangolo', 'rectangle', 'rect', 'quadrato', 'box', 'riquadro'],
    ['linea', 'line', 'riga'],
    ['luce', 'luci', 'light', 'lights', 'illuminazione'],
    ['camera', 'telecamera', 'visuale', 'inquadratura', 'scrolling', 'scorrimento'],
    ['controller', 'gamepad', 'pad', 'joystick', 'joypad'],
    ['tastiera', 'keyboard'],
    ['vita', 'vite', 'lives', 'life', 'salute', 'health', 'energia'],
    ['menu', 'titolo', 'title', 'schermata'],
    ['astronave', 'navicella', 'spaceship', 'ship', 'nave'],
    ['sprite', 'sprites', 'spr'],
]
SYN = {}
for group in SYNONYMS:
    for w in group:
        SYN.setdefault(w, []).extend(x for x in group if x != w)

NOISE = ['gioco', 'codice', 'bm', 'lua', 'fare', 'usare', 'ciao', 'aiuto', 'help', 'game', 'code',
         'esempio', 'example', 'scrivere', 'mio', 'semplice', 'simple', 'modo', 'way']


def typo(w, rng):
    if len(w) < 4:
        return w
    i = rng.randrange(1, len(w) - 1)
    op = rng.randrange(4)
    if op == 0:
        return w[:i] + w[i + 1] + w[i] + w[i + 2:]        # swap
    if op == 1:
        return w[:i] + w[i + 1:]                           # drop
    if op == 2:
        return w[:i] + w[i] + w[i:]                        # double
    return w[:i] + rng.choice('aeiourstlnm') + w[i + 1:]   # replace


def variants(phrase, rng, n):
    words = al.tokens(phrase)
    out = []
    for _ in range(n):
        ws = list(words)
        content = [i for i, w in enumerate(ws) if len(w) > 1 and w not in al._STOPSET]
        if len(content) >= 2 and rng.random() < 0.5:
            del ws[rng.choice(content)]
        content = [i for i, w in enumerate(ws) if len(w) > 1 and w not in al._STOPSET]
        for i in content:
            if ws[i] in SYN and rng.random() < 0.35:
                ws[i] = rng.choice(SYN[ws[i]])
        if content and rng.random() < 0.4:
            i = rng.choice(content)
            ws[i] = typo(ws[i], rng)
        if rng.random() < 0.25:
            ws.insert(rng.randrange(len(ws) + 1), rng.choice(NOISE))
        out.append(' '.join(ws))
    return out


def dataset(entries, rng, nvar):
    xs, ys = [], []
    for k, e in enumerate(entries):
        for p in e.phrases():
            for q in [p, p] + variants(p, rng, nvar):
                f = al.features(q)
                if f:
                    xs.append(f)
                    ys.append(k)
    return xs, np.array(ys)


def dense_batch(feats_list, nb):
    x = np.zeros((len(feats_list), nb), np.float32)
    for i, f in enumerate(feats_list):
        if f:
            x[i, f] = 1.0 / np.sqrt(len(f))
    return x


def train(xs, ys, ncls, hidden, epochs, seed, lr=0.004, l2=1e-6):
    """Adam on cross-entropy; the input is sparse (a few dozen features of
    4096), so the first layer is a sum of rows and its gradient goes only
    to those rows"""
    rs = np.random.RandomState(seed)
    nb = al.NBUCKETS
    w1 = (rs.randn(nb, hidden) * 0.1).astype(np.float32)
    b1 = np.zeros(hidden, np.float32)
    w2 = (rs.randn(hidden, ncls) * np.sqrt(2.0 / hidden)).astype(np.float32)
    b2 = np.zeros(ncls, np.float32)
    params = [w1, b1, w2, b2]
    m = [np.zeros_like(p) for p in params]
    v = [np.zeros_like(p) for p in params]
    t = 0
    n = len(xs)
    bs = 64
    feats = [np.array(f, np.int64) for f in xs]
    scale = [np.float32(1.0 / np.sqrt(len(f))) for f in xs]
    for ep in range(epochs):
        order = rs.permutation(n)
        loss_sum = 0.0
        for s in range(0, n, bs):
            idx = order[s:s + bs]
            rows = np.concatenate([np.full(len(feats[i]), k) for k, i in enumerate(idx)])
            cols = np.concatenate([feats[i] for i in idx])
            vals = np.concatenate([np.full(len(feats[i]), scale[i], np.float32) for i in idx])
            y = ys[idx]
            h = np.zeros((len(idx), hidden), np.float32)
            np.add.at(h, rows, w1[cols] * vals[:, None])
            h += b1
            hr = np.maximum(h, 0)
            mask = (rs.rand(*hr.shape) > 0.15).astype(np.float32) / 0.85
            hd = hr * mask
            z = hd @ w2 + b2
            z -= z.max(axis=1, keepdims=True)
            p = np.exp(z)
            p /= p.sum(axis=1, keepdims=True)
            loss_sum += -np.log(p[np.arange(len(y)), y] + 1e-9).sum()
            dz = p
            dz[np.arange(len(y)), y] -= 1
            dz /= len(y)
            gw2 = hd.T @ dz + l2 * w2
            gb2 = dz.sum(axis=0)
            dh = (dz @ w2.T) * mask * (h > 0)
            gw1 = l2 * w1
            np.add.at(gw1, cols, dh[rows] * vals[:, None])
            gb1 = dh.sum(axis=0)
            t += 1
            for i, g in enumerate((gw1, gb1, gw2, gb2)):
                m[i] = 0.9 * m[i] + 0.1 * g
                v[i] = 0.999 * v[i] + 0.001 * g * g
                mh = m[i] / (1 - 0.9 ** t)
                vh = v[i] / (1 - 0.999 ** t)
                params[i] -= lr * mh / (np.sqrt(vh) + 1e-8)
        if ep % 5 == 4 or ep == epochs - 1:
            print('  epoch %d: loss %.4f' % (ep + 1, loss_sum / n))
    return w1, b1, w2, b2


def quantize(w1, b1, w2, b2, calib):
    """float -> the integers of src/ai/nn.c (see assistlib.logits)"""
    hid = w1.shape[1]
    s1 = np.percentile(np.abs(w1), 99.99) / 127.0
    q1 = np.clip(np.round(w1 / s1), -127, 127).astype(np.int64)
    qb1 = np.round(b1 / s1).astype(np.int64)
    pool = np.array(al.pool_table(), np.int64)
    acts = []
    for f in calib:
        acc = q1[f].sum(axis=0) * pool[len(f)] >> 16
        acc = acc + qb1
        acts.append(acc[acc > 0])
    acts = np.concatenate(acts) if acts else np.array([1])
    top = max(int(np.percentile(acts, 99.9)), 1)
    sh = s1 * top / 127.0
    shift = 24
    mult = int(round((1 << shift) * 127.0 / top))
    while mult > (1 << 15):
        shift -= 1
        mult = int(round((1 << shift) * 127.0 / top))
    s2 = np.abs(w2).max() / 127.0
    q2 = np.clip(np.round(w2 / s2), -127, 127).astype(np.int64)       # hid x ncls
    qb2 = np.round(b2 / (s2 * sh)).astype(np.int64)
    return dict(w1=[int(v) for v in q1.reshape(-1)], b1=[int(v) for v in qb1], mult=mult, shift=shift,
                w2=[int(v) for v in q2.T.reshape(-1)], b2=[int(v) for v in qb2], scale=float(s2 * sh))


def int_logits(net, feats_list, hid):
    """the integer network over many questions (numpy, same integers)"""
    q1 = np.array(net['w1'], np.int64).reshape(-1, hid)
    qb1 = np.array(net['b1'], np.int64)
    q2 = np.array(net['w2'], np.int64).reshape(-1, hid)
    qb2 = np.array(net['b2'], np.int64)
    pool = al.pool_table()
    half = 1 << (net['shift'] - 1)
    out = []
    for f in feats_list:
        acc = (q1[f].sum(axis=0) * pool[len(f)] >> 16) + qb1 if f else qb1.copy()
        y = np.where(acc > 0, np.minimum(127, (acc * net['mult'] + half) >> net['shift']), 0)
        out.append(q2 @ y + qb2)
    return np.array(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[1])
    ap.add_argument('--kb', default=os.path.join(ROOT, 'src/ai/kb'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'src/ai/assist.weights'))
    ap.add_argument('--epochs', type=int, default=30)
    ap.add_argument('--hidden', type=int, default=64)
    ap.add_argument('--variants', type=int, default=8)
    ap.add_argument('--seed', type=int, default=1)
    a = ap.parse_args()

    t0 = time.time()
    entries = al.parse_kb(al.kb_paths(a.kb))
    tests = al.parse_tests(os.path.join(a.kb, 'tests.txt'))
    ids = [e.id for e in entries]
    for q, want, ln in tests:
        for w in want:
            if w not in ids:
                sys.exit('tests.txt:%d: no entry %r' % (ln, w))
    rng = random.Random(a.seed)
    xs, ys = dataset(entries, rng, a.variants)
    print('%d entries, %d examples' % (len(entries), len(xs)))
    w1, b1, w2, b2 = train(xs, ys, len(entries), a.hidden, a.epochs, a.seed)

    # accuracy on the held-out questions: float, then integer
    tf = [al.features(q) for q, _, _ in tests]
    xf = dense_batch(tf, al.NBUCKETS)
    zf = np.maximum(xf @ w1 + b1, 0) @ w2 + b2
    net = quantize(w1, b1, w2, b2, xs[::3])
    zi = int_logits(net, tf, a.hidden)
    for name, z in (('float', zf), ('int8', zi)):
        top1 = top3 = 0
        for k, (q, want, ln) in enumerate(tests):
            best = [ids[i] for i in np.argsort(-z[k])[:3]]
            top1 += best[0] in want
            top3 += any(b in want for b in best)
        print('%s: held-out top-1 %d/%d, top-3 %d/%d' % (name, top1, len(tests), top3, len(tests)))
    misses = []
    for k, (q, want, ln) in enumerate(tests):
        best = [ids[i] for i in np.argsort(-zi[k])[:3]]
        if best[0] not in want:
            misses.append('  tests.txt:%d %r -> %s (want %s)' % (ln, q, ', '.join(best), ', '.join(want)))
    if misses:
        print('not first:\n' + '\n'.join(misses))
    # the pure-Python reference agrees with the numpy integers
    for k in range(min(5, len(tests))):
        ref = al.logits(net, tf[k])
        assert ref == [int(v) for v in zi[k]], 'integer network mismatch'
    al.save_weights(a.out, ids, net)
    print('%s: %d bytes (%.1f s)' % (a.out, os.path.getsize(a.out), time.time() - t0))


if __name__ == '__main__':
    main()
