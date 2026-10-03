#!/usr/bin/env python3
"""
brain.py - the bots' network (M38.4): it learns which tactic pays, from
matches the bots play on the PC (bmhost, ten bots, no drawing, logging
every choice).

  brain.py BUILD_DIR [--gens 4] [--matches 40] [--out carts/overbit/src/76_brain.lua]

1. Generation 0: the bots choose with the rules (Bots.teacher in
   src/75_bots.lua), and at random one time in five, so that every tactic
   is tried in every kind of moment.
2. Each choice gets what followed in the next seconds, for the bot and its
   team: kills, deaths, the damage done and the healing, the point's
   capture and the percentages (a discounted sum: the return).
3. A value network learns the return expected from a moment; the
   advantage of a choice is how much better it went. The policy network
   (24 numbers -> 32 -> 32 -> 6 tactics) learns the choices taken, each
   weighted by exp(advantage): the good ones count more, the bad less
   (advantage-weighted regression: it stays near what was tried).
4. The next generations play with the policy (and still some chance),
   learning from all the matches so far.
5. The test: one team with the network, the other with the rules, each
   side half the time; the network is written (INT8, src/76_brain.lua,
   Bots.BRAIN, read by nnet()).
"""
import argparse
import math
import os
import random
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..", "..")
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import nnetlib  # noqa: E402

NF, NT = 24, 6
TACTICS = ["point", "fight", "retreat", "flank", "guard", "hold"]
HORIZON, GAMMA = 15.0, 0.9          # seconds looked ahead, discount per second


# ------------------------------------------------------------------ playing

def build_cart(build, name, defines):
    """a match of ten bots, logging, with these globals"""
    lua = os.path.join(build, "overbit", f"{name}.lua")
    cart = os.path.join(build, "overbit", f"{name}.bm")
    args = [sys.executable, os.path.join(HERE, "..", "build.py"), lua, "--start", "match",
            "--extra", os.path.join(build, "overbit", "21_map.lua")]
    for d in defines:
        args += ["--define", d]
    subprocess.run(args, check=True)
    subprocess.run([sys.executable, os.path.join(ROOT, "scripts", "mkbm.py"), "-o", cart, "--lua", lua,
                    "--title", "brain", "--author", "bm", "--res", "320x180",
                    "--models", os.path.join(build, "overbit", "models.bm"),
                    "--audio", os.path.join(build, "overbit", "sounds.json")], check=True, capture_output=True)
    return cart


def play(build, cart, seconds):
    r = subprocess.run([os.path.join(build, "host", "bmhost-bin"), cart, "--seconds", str(seconds)],
                       capture_output=True, text=True)
    return r.stdout + r.stderr


def parse(log):
    """the choices: (t, id, team, tactic, features, stats); the match's end"""
    out, winner = [], 0
    for line in log.splitlines():
        if line.startswith("ai d "):
            v = line.split()
            t, aid, team, tac = float(v[2]), int(v[3]), int(v[4]), int(v[5]) - 1
            f = [float(x) for x in v[6:6 + NF]]
            st = [float(x) for x in v[6 + NF:]]
            out.append((t, aid, team, tac, f, st))
        m = re.match(r"overbit match team (\d)", line)
        if m:
            winner = int(m.group(1))
    return out, winner


def returns(choices):
    """what followed each choice: the stats of the same bot at its later
    choices (kills, deaths, damage, healing, its team's point), discounted"""
    by = {}
    for c in choices:
        by.setdefault(c[1], []).append(c)
    X, A, R = [], [], []
    for aid, cs in by.items():
        for i, (t, _, team, tac, f, st) in enumerate(cs):
            g, k, d0 = 0.0, 0.0, None
            for j in range(i + 1, len(cs)):
                t2, st2 = cs[j][0], cs[j][5]
                if t2 - t > HORIZON:
                    break
                prev = cs[j - 1][5]
                kills, deaths, dealt, healed, mine, theirs, cap = (st2[q] - prev[q] for q in range(7))
                # the team's point counts most; then staying alive, then the rest
                r = (0.4 * kills - 0.8 * deaths + dealt / 500 + healed / 400 + (mine - theirs) / 3 + 2.0 * cap)
                g += r * GAMMA ** (t2 - t)
            X.append(f)
            A.append(tac)
            R.append(g)
    return np.array(X, float), np.array(A, int), np.array(R, float)


# ------------------------------------------------------------------ learning

def init(sizes, rng):
    return [(rng.normal(0, np.sqrt(2 / a), (b, a)), np.zeros(b)) for a, b in zip(sizes[:-1], sizes[1:])]


def forward(layers, X):
    hs = [X]
    h = X
    for i, (W, b) in enumerate(layers):
        h = h @ W.T + b
        if i < len(layers) - 1:
            h = np.maximum(h, 0)
        hs.append(h)
    return hs


def fit(X, Y, out, loss, epochs=60, lr=2e-3, decay=1e-4, seed=1, weights=None):
    """a network 24 -> 32 -> 32 -> out by Adam; loss "mse" (Y: targets) or
    "ce" (Y: classes, weighted); the epoch with the best error on a tenth
    kept aside"""
    rng = np.random.default_rng(seed)
    perm = rng.permutation(len(X))
    nv = len(X) // 10
    vi, ti = perm[:nv], perm[nv:]
    W = np.ones(len(X)) if weights is None else weights
    layers = init([NF, 32, 32, out], rng)
    params = [p for L in layers for p in L]
    m = [np.zeros_like(p) for p in params]
    v = [np.zeros_like(p) for p in params]
    step, best, best_err = 0, None, math.inf

    def grad_of(q, yb, wb):
        if loss == "mse":
            g = (q[:, 0] - yb)[:, None] * wb[:, None]
            return g / len(yb), float(np.sum(wb * (q[:, 0] - yb) ** 2) / np.sum(wb))
        z = q - q.max(1, keepdims=True)
        p = np.exp(z)
        p /= p.sum(1, keepdims=True)
        rows = np.arange(len(yb))
        err = float(-np.sum(wb * np.log(p[rows, yb] + 1e-9)) / np.sum(wb))
        p[rows, yb] -= 1
        return p * wb[:, None] / np.sum(wb), err
    for ep in range(epochs):
        idx = ti[rng.permutation(len(ti))]
        for s0 in range(0, len(idx), 256):
            bi = idx[s0:s0 + 256]
            hs = forward(layers, X[bi])
            g, _ = grad_of(hs[-1], Y[bi], W[bi])
            grads = [None] * len(params)
            for i in range(len(layers) - 1, -1, -1):
                Wl, _ = layers[i]
                grads[2 * i], grads[2 * i + 1] = g.T @ hs[i] + decay * Wl, g.sum(0)
                if i > 0:
                    g = (g @ Wl) * (hs[i] > 0)
            step += 1
            for k, (p, gk) in enumerate(zip(params, grads)):
                m[k] = 0.9 * m[k] + 0.1 * gk
                v[k] = 0.999 * v[k] + 0.001 * gk * gk
                p -= lr * (m[k] / (1 - 0.9 ** step)) / (np.sqrt(v[k] / (1 - 0.999 ** step)) + 1e-8)
        _, err = grad_of(forward(layers, X[vi])[-1], Y[vi], W[vi])
        if err < best_err:
            best_err, best = err, [(Wl.copy(), b.copy()) for Wl, b in layers]
    print(f"    {loss}: best kept-aside error {best_err:.3f}")
    return best


def train(X, A, R, seed=1):
    """advantage-weighted regression: the value of a moment, then the
    choices weighted by how much better than that they went"""
    value = fit(X, R, 1, "mse", seed=seed)
    adv = R - forward(value, X)[-1][:, 0]
    beta = max(0.05, float(np.std(adv)) * 0.5)
    w = np.exp(np.clip(adv / beta, -4, 2.5))
    for t in range(NT):
        sel = A == t
        if sel.any():
            print(f"    {TACTICS[t]:8s} taken {sel.mean() * 100:4.1f}%, advantage {adv[sel].mean():+.3f}")
    return fit(X, A, NT, "ce", seed=seed + 7, weights=w)


def to_q(layers, X):
    return nnetlib.quantize([(W, b, i < len(layers) - 1) for i, (W, b) in enumerate(layers)], X)


# ------------------------------------------------------------------ all of it

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("build")
    ap.add_argument("--gens", type=int, default=4)
    ap.add_argument("--matches", type=int, default=40)
    ap.add_argument("--seconds", type=int, default=300)
    ap.add_argument("--eval", type=int, default=40)
    ap.add_argument("--eval-seconds", type=int, default=600)
    ap.add_argument("--out", default=os.path.join(HERE, "..", "src", "76_brain.lua"))
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    a = ap.parse_args()
    rules = "OVERBIT_RULES={unlock=3,pct=0.6,round_end=1,match_end=1}"
    data = []
    blob = None
    for gen in range(a.gens + 1):
        defines = ["OVERBIT_SPECTATE=true", "OVERBIT_HEADLESS=true", rules, "OVERBIT_AI_LOG=true",
                   "OVERBIT_AI_EXPLORE=0.2"]
        if blob:
            defines.append(f"OVERBIT_AI_NET={nnetlib.lua_string(blob)}")
        with ThreadPoolExecutor(a.jobs) as ex:
            carts = list(ex.map(lambda k: build_cart(a.build, f"brain-{gen}-{k}",
                                                     defines + [f"OVERBIT_SEED={gen * 1000 + k + 1}"]),
                                range(a.matches)))
            logs = list(ex.map(lambda c: play(a.build, c, a.seconds), carts))
        for c in carts:
            os.unlink(c)
        n0 = len(data)
        for log in logs:
            ch, _ = parse(log)
            data.append(returns(ch))
        X = np.concatenate([d[0] for d in data])
        A = np.concatenate([d[1] for d in data])
        R = np.concatenate([d[2] for d in data])
        print(f"generation {gen}: {len(data) - n0} matches, {len(X)} choices so far, mean return {R.mean():.3f}")
        if gen == a.gens:
            break
        layers = train(X, A, R, seed=gen + 1)
        q = to_q(layers, X)
        blob = nnetlib.pack(q)
        # how the choices of the network differ from the rules
        Q = forward(layers, X)[-1]
        pick = Q.argmax(1)
        print(f"    the network agrees with the choice taken {np.mean(pick == A) * 100:.0f}% of the time")
        print("    the network picks: " + ", ".join(f"{TACTICS[t]} {np.mean(pick == t) * 100:.0f}%" for t in range(NT)))
    # the test: blue with the network, red with the rules
    # the test: one team with the network, the other with the rules (each
    # side half the time)
    won = lost = left = 0
    def eval_cart(k):
        team = 1 + k % 2
        d = ["OVERBIT_SPECTATE=true", "OVERBIT_HEADLESS=true", rules, f"OVERBIT_AI_NET={nnetlib.lua_string(blob)}",
             f"OVERBIT_AI_NET_TEAM={team}", f"OVERBIT_SEED={9000 + k}"]
        return team, build_cart(a.build, f"brain-eval-{k}", d)
    with ThreadPoolExecutor(a.jobs) as ex:
        carts = list(ex.map(eval_cart, range(a.eval)))
        logs = list(ex.map(lambda tc: (tc[0], play(a.build, tc[1], a.eval_seconds)), carts))
    for _, c in carts:
        os.unlink(c)
    for team, log in logs:
        _, w = parse(log)
        if w == team: won += 1
        elif w: lost += 1
        else: left += 1
    wins = (left, won, lost)
    print(f"the network against the rules: {won} won, {lost} lost, {left} unfinished")
    with open(a.out, "w") as f:
        f.write("-- generated by carts/overbit/art/brain.py: do not edit\n")
        f.write(f"-- the bots' tactics, {NF} inputs -> {NT} tactics; against the rules: "
                f"{wins[1]} won, {wins[2]} lost of {a.eval}\n")
        f.write(f"Bots.BRAIN = {nnetlib.lua_string(blob)}\n")
    print(f"{a.out}: {len(blob)} bytes")


if __name__ == "__main__":
    main()
