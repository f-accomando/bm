"""
nnetlib.py - the small integer networks of the cartridges (src/ai/net.h):
a float MLP (numpy) quantized to INT8, packed as a "BMNN" blob for nnet(),
and the integer reference that gives the same numbers as the C code.

  layers = [(W, b, relu), ...]      W: (outputs, inputs) floats
  q = quantize(layers, X)           X: inputs to calibrate the scales
  blob = pack(q)
  y = run_q(q, x)                   the outputs, as nnet():run()
  lua = lua_string(blob)            a Lua string literal of the blob
"""
import math
import struct

import numpy as np


def quantize(layers, X):
    """layers of floats -> {"in_scale", "out_scale", "layers": [(Wq, bq, relu, mult, shift)]};
    the scales from the largest values the inputs X give (calibration)"""
    X = np.asarray(X, float)
    s_in = 127.0 / max(1e-6, np.percentile(np.abs(X), 99.9))
    out, s_x, a = [], s_in, X
    for i, (W, b, relu) in enumerate(layers):
        W, b = np.asarray(W, float), np.asarray(b, float)
        s_w = 127.0 / max(1e-6, np.abs(W).max())
        Wq = np.clip(np.round(W * s_w), -127, 127).astype(np.int8)
        bq = np.round(b * s_w * s_x).astype(np.int64)
        a = a @ W.T + b
        if relu:
            a = np.maximum(a, 0)
            s_a = 127.0 / max(1e-6, np.percentile(a, 99.9))
            r = s_a / (s_w * s_x)
            shift = int(min(40, max(1, 30 - math.ceil(math.log2(r)))))
            mult = int(round(r * (1 << shift)))
            out.append((Wq, bq, True, mult, shift))
            s_x = s_a
        else:
            out.append((Wq, bq, False, 0, 0))
            out_scale = 1.0 / (s_w * s_x)
    return {"in_scale": float(s_in), "out_scale": float(out_scale), "layers": out}


def pack(q):
    L = q["layers"]
    nin = L[0][0].shape[1]
    out = bytearray(b"BMNN" + struct.pack("<BBHff", 1, len(L), nin, q["in_scale"], q["out_scale"]))
    for Wq, bq, relu, mult, shift in L:
        nout, n = Wq.shape
        n4 = (n + 3) & ~3
        out += struct.pack("<HBBi", nout, 1 if relu else 0, shift, mult)
        out += struct.pack(f"<{nout}i", *[int(v) for v in bq])
        W4 = np.zeros((nout, n4), np.int8)
        W4[:, :n] = Wq
        out += W4.tobytes()
    return bytes(out)


def run_q(q, x):
    """the outputs for one input vector, in integers exactly as net.c"""
    v = np.asarray(x, float) * q["in_scale"]
    xq = np.clip(np.where(v < 0, np.ceil(v - 0.5), np.floor(v + 0.5)), -127, 127).astype(np.int64)
    for Wq, bq, relu, mult, shift in q["layers"]:
        acc = Wq.astype(np.int64) @ xq + bq
        acc = ((acc + 2 ** 31) % 2 ** 32) - 2 ** 31            # int32 sums
        if relu:
            y = np.where(acc <= 0, 0, (acc * mult + (1 << (shift - 1))) >> shift)
            xq = np.minimum(y, 127)
        else:
            return acc * q["out_scale"]
    return None


def lua_string(blob):
    """a Lua string literal with the bytes (decimal escapes)"""
    parts = []
    for i, c in enumerate(blob):
        if 32 <= c < 127 and c not in (34, 92) and not (chr(c).isdigit() and parts and parts[-1].startswith("\\")):
            parts.append(chr(c))
        else:
            parts.append(f"\\{c}")
    return '"' + "".join(parts) + '"'
